#!/usr/bin/env python3
"""Verify original-family BSD readback; distinguish APFS init from a mount/boot."""

import argparse
import json
from pathlib import Path
import re

from native_xnu_result import assess as assess_kernel
from native_ios_root_contract import (
    RAW_BYTES,
    RAW_SHA256,
    PREFIX_FNV64,
    TAIL_BYTES,
    TAIL_FNV64,
    VOLUME_UUID,
)

APPOS_SHA256 = "52e568e1eaaede5fde4684ad10afc60f8fb236bfb5a23e0869bb0ed8a1d275d7"


def assess(console: str, runner: str, contract: dict, pattern: dict) -> dict:
    nonce = contract["nonce"]
    appos = contract.get("fixture_kind") == "official_appos"
    main_os = contract.get("fixture_kind") == "official_main_os"
    mount_probe = contract.get("mount_probe") is True
    if (mount_probe and not (appos or main_os)) or (main_os and not mount_probe):
        raise ValueError("Mount diagnostic requires a separately pinned official disk")
    expected_bytes = RAW_BYTES if main_os else (10485760 if appos else 2097152)
    if pattern["nonce"] != nonce or pattern["disk_bytes"] != expected_bytes:
        raise ValueError("Mixed or invalid diagnostic disk identity")
    if appos and pattern.get("disk_sha256") != APPOS_SHA256:
        raise ValueError("Wrong official AppOS fixture")
    if main_os and (
        pattern.get("disk_sha256") != RAW_SHA256
        or pattern.get("prefix_fnv64") != PREFIX_FNV64
        or pattern.get("prefix_bytes") != 4096
        or pattern.get("tail_bytes") != TAIL_BYTES
        or pattern.get("tail_offset") != RAW_BYTES - TAIL_BYTES
        or pattern.get("volume_uuid") != VOLUME_UUID
        or pattern.get("tail_fnv64") != TAIL_FNV64
    ):
        raise ValueError("Wrong main OS geometry, digest or bounded-read contract")
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
    if main_os:
        if len(rows) < 9 or rows[7][0:3] != ("ORIGINAL", nonce, "bsd_gpt_prefix_pass"):
            raise ValueError("Main OS GPT prefix BSD receipt missing")
        if rows[8] != (
            "ORIGINAL",
            nonce,
            "bsd_partial_tail_pass",
            str(TAIL_BYTES),
            str(pattern["tail_fnv64"]),
        ):
            raise ValueError("Main OS partial final-page BSD receipt missing or changed")
        # Normalize only after validating the distinct, bounded-read evidence.
        rows[7] = ("ORIGINAL", nonce, "bsd_read_pass", rows[7][3], rows[7][4])
        rows.pop(8)
        roots = re.findall(
            r"PLB_XNU_REGISTRY nonce=([0-9a-f]{32}) depth=\d+ class=(\S+) bsd=(\S+) uuid="
            + VOLUME_UUID
            + r"(?:\r?\n|$)",
            console,
        )
        if len(roots) != 1 or roots[0][0] != nonce or roots[0][1] != "AppleAPFSVolume":
            raise ValueError("Main root UUID was not uniquely observed on the actual APFS volume")
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
    mount_error = None
    auth_error = None
    if mount_probe:
        mount_rows = [row for row in rows if row[2] == "vfs_mount_result"]
        if len(mount_rows) != 1:
            raise ValueError("Missing or ambiguous actual VFS mount result")
        mount_error = int(mount_rows[0][3])
        if not 0 <= mount_error <= 4095:
            raise ValueError("Invalid VFS errno")
        phases = phases[:9] + [("ORIGINAL", "root_selected"), ("ORIGINAL", "vfs_mount_result")]
        if mount_error == 0:
            phases += [("ORIGINAL", "vfs_apfs_readonly"), ("ORIGINAL", "apple_root_auth_result")]
        phases += [
            ("ORIGINAL", "device_reset"),
            ("ORIGINAL", "mount_probe_complete"),
            ("ORIGINAL", "guest_shutdown"),
        ]
    if appos:
        phases.insert(7, ("ORIGINAL", "apfs_container_header"))
    if [(row[0], row[2]) for row in rows] != phases or any(row[1] != nonce for row in rows):
        raise ValueError("Incomplete or ambiguous original-family evidence")
    values = [(int(row[3]), int(row[4])) for row in rows]
    if appos:
        if values.pop(7) != (expected_bytes, 4096):
            raise ValueError("Real APFS container header was not observed")
    if values[0] != (0, 0) or values[1] != (144, 144):
        raise ValueError("Live original class layout was not qualified")
    if {values[2][1], values[3][1]} != {expected_bytes // 512, 131072} or values[2][0] == values[3][
        0
    ]:
        raise ValueError("Unexpected or duplicate PCI devices")
    if any(not 0 <= values[index][0] < 256 for index in (2, 3)):
        raise ValueError("Invalid PCI function")
    if values[4] != (0x9FFF0000, 8192) or values[5] != (0, 0):
        raise ValueError("Wrong DMA pool or block startup failure")
    if not 0 <= values[6][0] <= 255 or not 0 <= values[6][1] <= 0xFFFFFF:
        raise ValueError("Invalid BSD device identity")
    expected_read = (4096, PREFIX_FNV64) if main_os else (expected_bytes, pattern["disk_fnv64"])
    if values[7] != expected_read:
        raise ValueError("Full original-family BSD readback failed")
    if values[8] != (0, 0) or values[-3:] != [(0, 0)] * 3:
        raise ValueError("Device not closed/reset/shut down")
    if mount_probe:
        if values[9][0] != 1 or not 1 <= values[9][1] <= 128 or values[10] != (mount_error, 0):
            raise ValueError("Invalid unique root selection/mount result")
        if mount_error == 0:
            if values[11] != (1, 1):
                raise ValueError("The actual mount is not read-only APFS")
            auth_error, unused = values[12]
            if not 0 <= auth_error <= 4095 or unused != 0:
                raise ValueError("Invalid actual root authentication errno")
    apfs = re.search(
        r"apfs_module_start:\d+: load: com\.apple\.filesystems\.apfs, v([^,\s]+)", console
    )
    return {
        "original_storage_family_bsd_readback_verified": True,
        "bytes_verified": 4096 + TAIL_BYTES if main_os else expected_bytes,
        "guest_full_disk_readback_verified": not main_os,
        "disk_capacity_bytes": expected_bytes,
        "gpt_prefix_and_partial_tail_verified": main_os,
        "fixture_kind": (
            "official_main_os" if main_os else ("official_appos" if appos else "nonce_pattern")
        ),
        "apfs_container_header_verified": appos,
        "live_original_base_class_bytes": 144,
        "bsd_device_closed": True,
        "original_corecrypto_post_passed": True,
        "apfs_initialization_observed": bool(apfs) and "done registering sysctls." in console,
        "apfs_version": apfs.group(1) if apfs else None,
        "guest_shutdown_verified": True,
        "supervisor_exit": 0,
        "filesystem_mount_verified": mount_probe and mount_error == 0,
        "root_mount_errno": mount_error,
        "root_authentication_evaluated": auth_error is not None,
        "root_authentication_errno": auth_error,
        "root_authentication_verified": auth_error == 0,
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
