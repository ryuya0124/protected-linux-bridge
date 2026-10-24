#!/usr/bin/env python3
"""Verify original-family BSD readback; distinguish APFS init from a mount/boot."""

import argparse
import json
from pathlib import Path
import re

from native_xnu_result import assess as assess_kernel


def assess(console: str, runner: str, contract: dict, pattern: dict) -> dict:
    nonce = contract["nonce"]
    if pattern["nonce"] != nonce or pattern["disk_bytes"] != 2097152:
        raise ValueError("Mixed or invalid synthetic disk identity")
    if contract.get("source_storage_archive_linked") is not False:
        raise ValueError("This is not the isolated original-storage variant")
    if re.findall(r"PLB_NATIVE_XNU_RUNNER nonce=([0-9a-f]{32}) result=(\d+)", runner) != [
        (nonce, "0")
    ]:
        raise ValueError("Fresh supervisor did not finish normally")
    firmware = contract.get("firmware_nonce", nonce)
    kernel = assess_kernel(console, f"PLB_NATIVE_XNU_RUNNER nonce={firmware} result=0\n", firmware)
    if not kernel["xnu_serial_execution_observed"] or kernel["guest_panic"] is not None:
        raise ValueError("Kernel execution failed")
    if not kernel["crypto_post_all_passed_observed"]:
        raise ValueError("Original corecrypto did not complete POST")
    rows = re.findall(
        r"PLB_XNU_(ORIGINAL|MEDIA) nonce=([0-9a-f]{32}) phase=([a-z_]+) a=(\d+) b=(\d+)",
        console,
    )
    phases = [
        ("ORIGINAL", "entry"),
        ("ORIGINAL", "base_class_size"),
        ("MEDIA", "candidate"),
        ("MEDIA", "candidate"),
        ("MEDIA", "queue_ready"),
        ("ORIGINAL", "block_started"),
        ("ORIGINAL", "bsd_node"),
        ("ORIGINAL", "bsd_read_pass"),
        ("ORIGINAL", "bsd_closed"),
        ("ORIGINAL", "device_reset"),
        ("ORIGINAL", "pass"),
        ("ORIGINAL", "guest_shutdown"),
    ]
    if [(row[0], row[2]) for row in rows] != phases or any(row[1] != nonce for row in rows):
        raise ValueError("Incomplete or ambiguous original-family evidence")
    values = [(int(row[3]), int(row[4])) for row in rows]
    if values[0] != (0, 0) or values[1] != (144, 144):
        raise ValueError("Live original class layout was not qualified")
    if {values[2][1], values[3][1]} != {4096, 131072} or values[2][0] == values[3][0]:
        raise ValueError("Unexpected or duplicate PCI devices")
    if any(not 0 <= values[index][0] < 256 for index in (2, 3)):
        raise ValueError("Invalid PCI function")
    if values[4] != (0x9FFF0000, 8192) or values[5] != (0, 0):
        raise ValueError("Wrong DMA pool or block startup failure")
    if not 0 <= values[6][0] <= 255 or not 0 <= values[6][1] <= 0xFFFFFF:
        raise ValueError("Invalid BSD device identity")
    if values[7] != (2097152, pattern["disk_fnv64"]):
        raise ValueError("Full original-family BSD readback failed")
    if values[8:] != [(0, 0)] * 4:
        raise ValueError("Device not closed/reset/shut down")
    apfs = re.search(
        r"apfs_module_start:\d+: load: com\.apple\.filesystems\.apfs, v([^,\s]+)", console
    )
    return {
        "original_storage_family_bsd_readback_verified": True,
        "bytes_verified": 2097152,
        "live_original_base_class_bytes": 144,
        "bsd_device_closed": True,
        "original_corecrypto_post_passed": True,
        "apfs_initialization_observed": bool(apfs) and "done registering sysctls." in console,
        "apfs_version": apfs.group(1) if apfs else None,
        "guest_shutdown_verified": True,
        "supervisor_exit": 0,
        "filesystem_mount_verified": False,
        "ios_boot_verified": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("console", "runner", "contract", "pci-contract"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    print(
        json.dumps(
            assess(
                args.console.read_text(),
                args.runner.read_text(),
                json.loads(args.contract.read_text()),
                json.loads(args.pci_contract.read_text()),
            ),
            indent=2,
        )
    )
