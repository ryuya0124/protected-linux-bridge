#!/usr/bin/env python3
"""Check nonce-bound guest UART evidence separately from the native host runner's result."""

import json
from pathlib import Path
import re


def assess(console: str, runner_output: str, contract: dict) -> dict:
    nonce = contract["nonce"]
    if not re.fullmatch(r"[0-9a-f]{32}", nonce):
        raise ValueError("Invalid evidence nonce")
    rows = re.findall(
        r"PLB_NATIVE_UEFI nonce=([0-9a-f]{32}) phase=([a-z_]+) a=(\d+) b=(\d+)", console
    )
    phases = [row[1] for row in rows]
    if phases != ["entry", "dma_window", "read_pass", "pass", "guest_shutdown"]:
        raise ValueError("Incomplete or failed native UEFI evidence")
    if any(row[0] != nonce for row in rows):
        raise ValueError("Evidence belongs to another trial")
    values = [(int(row[2]), int(row[3])) for row in rows]
    base, size = values[1]
    if values[0] != (1, 0):
        raise ValueError("Guest did not report EL1 execution")
    if size != 32 * 1024 * 1024 or base & 4095 or not 0x80000000 <= base <= 0xA0000000 - size:
        raise ValueError("Unexpected shared DMA window")
    if values[2] != (contract["disk_bytes"], contract["disk_fnv64"]):
        raise ValueError("Synthetic disk content verification did not complete")
    if values[3:] != [(0, 0), (0, 0)]:
        raise ValueError("Guest did not request a successful shutdown")
    host_results = re.findall(
        r"PLB_NATIVE_UEFI_RUNNER nonce=([0-9a-f]{32}) result=(\d+)", runner_output
    )
    if host_results != [(nonce, "0")]:
        raise ValueError("Host supervisor did not finish successfully")
    return {
        "guest_entry_verified": True,
        "guest_el": 1,
        "synthetic_disk_bytes_verified": contract["disk_bytes"],
        "guest_shutdown_requested": True,
        "native_runner_exit": 0,
        "cpu_backend": "Gunyah hardware virtualization",
        "ios_boot_verified": False,
    }


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--contract", required=True, type=Path)
    arguments = parser.parse_args()
    print(
        json.dumps(
            assess(
                arguments.console.read_text(),
                arguments.runner.read_text(),
                json.loads(arguments.contract.read_text()),
            ),
            indent=2,
        )
    )
