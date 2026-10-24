#!/usr/bin/env python3
"""Verify genuine synthetic IOMedia/BSD-node evidence, never an iOS/rootfs boot."""

import argparse
import json
from pathlib import Path
import re

from native_xnu_result import assess as assess_kernel


def assess(console: str, runner: str, contract: dict, pci: dict) -> dict:
    nonce = contract["nonce"]
    if pci["nonce"] != nonce:
        raise ValueError("Mixed media/pattern trial identities")
    results = re.findall(r"PLB_NATIVE_XNU_RUNNER nonce=([0-9a-f]{32}) result=(\d+)", runner)
    if results != [(nonce, "0")]:
        raise ValueError("The fresh supervisor did not complete normally")
    firmware_nonce = contract.get("firmware_nonce", nonce)
    kernel = assess_kernel(
        console, f"PLB_NATIVE_XNU_RUNNER nonce={firmware_nonce} result=0\n", firmware_nonce
    )
    if not kernel["xnu_serial_execution_observed"] or kernel["guest_panic"] is not None:
        raise ValueError("The source XNU kernel failed")
    rows = re.findall(
        r"PLB_XNU_MEDIA nonce=([0-9a-f]{32}) phase=([a-z_]+) a=(\d+) b=(\d+)", console
    )
    phases = [
        "entry",
        "candidate",
        "candidate",
        "queue_ready",
        "media_published",
        "bsd_node",
        "media_read_pass",
        "cross_page_pass",
        "write_refused",
        "device_reset",
        "pass",
        "guest_shutdown",
    ]
    if [row[1] for row in rows] != phases or any(row[0] != nonce for row in rows):
        raise ValueError("Incomplete or ambiguous media evidence")
    values = [(int(row[2]), int(row[3])) for row in rows]
    if values[0] != (0, 0) or values[-3:] != [(0, 0)] * 3:
        raise ValueError("The owned device was not reset/shut down")
    if {values[1][1], values[2][1]} != {4096, 131072} or values[1][0] == values[2][0]:
        raise ValueError("Unexpected or duplicate PCI disks")
    if any(not 0 <= values[index][0] < 256 for index in (1, 2)):
        raise ValueError("Invalid PCI function")
    if values[3:6] != [(0x9FFF0000, 8192), (2097152, 0), (1, 0)]:
        raise ValueError("Private DMA, writable media or absent BSD node")
    if pci["disk_bytes"] != 2097152 or values[6] != (2097152, pci["disk_fnv64"]):
        raise ValueError("Incomplete/wrong media readback")
    if values[7] != (512, 4096):
        raise ValueError("Cross-page media read not verified")
    if values[8][0] != values[8][1] or values[8][0] < 514:
        raise ValueError("Write was not refused without issuing device I/O")
    return {
        "iomedia_readback_verified": True,
        "bytes_verified": 2097152,
        "bsd_node_published": True,
        "cross_page_read_verified": True,
        "write_refused_without_device_io": True,
        "guest_shutdown_verified": True,
        "supervisor_exit": 0,
        "root_filesystem_boot_verified": False,
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
