#!/usr/bin/env python3
"""Verify full nonce-bound guest PCI/DMA readback, not an iOS or filesystem boot."""

import argparse
import json
from pathlib import Path
import re
from native_xnu_result import assess as assess_kernel


def assess(console: str, runner: str, contract: dict, pci: dict) -> dict:
    nonce = contract["nonce"]
    firmware_nonce = contract.get("firmware_nonce", nonce)
    # The boot firmware may be reused, but the kernel/pattern trial never is.
    results = re.findall(r"PLB_NATIVE_XNU_RUNNER nonce=([0-9a-f]{32}) result=(\d+)", runner)
    if results != [(nonce, "0")]:
        raise ValueError("The fresh trial's supervisor did not complete")
    kernel_runner = f"PLB_NATIVE_XNU_RUNNER nonce={firmware_nonce} result=0\n"
    kernel = assess_kernel(console, kernel_runner, firmware_nonce)
    if (
        not kernel["xnu_serial_execution_observed"]
        or kernel["guest_panic"] is not None
        or kernel["host_supervisor_exit"] != 0
    ):
        raise ValueError("Native XNU/supervisor did not complete")
    if pci["nonce"] != nonce:
        raise ValueError("The pattern belongs to another trial")
    rows = re.findall(r"PLB_XNU_PCI nonce=([0-9a-f]{32}) phase=([a-z_]+) a=(\d+) b=(\d+)", console)
    if [r[1] for r in rows] != [
        "entry",
        "candidate",
        "candidate",
        "queue_ready",
        "read_pass",
        "pass",
        "guest_shutdown",
    ]:
        raise ValueError("Incomplete or failed PCI/DMA readback")
    if any(r[0] != nonce for r in rows):
        raise ValueError("Mixed PCI trial identities")
    values = [(int(r[2]), int(r[3])) for r in rows]
    if values[0] != (0, 0) or values[-2:] != [(0, 0), (0, 0)]:
        raise ValueError("Guest success/shutdown receipt missing")
    candidates = values[1:3]
    if (
        {v[1] for v in candidates} != {4096, 131072}
        or len({v[0] for v in candidates}) != 2
        or any(not 0 <= v[0] < 256 for v in candidates)
    ):
        raise ValueError("Unexpected guest block devices")
    if values[3] != (0x9FFF0000, 8192):
        raise ValueError("The queue did not use the reserved shared tail")
    if pci["disk_bytes"] != 2097152 or values[4] != (pci["disk_bytes"], pci["disk_fnv64"]):
        raise ValueError("Not every synthetic disk byte was verified")
    return {
        "xnu_pci_readback_verified": True,
        "bytes_verified": 2097152,
        "guest_shutdown_verified": True,
        "supervisor_exit": 0,
        "host_modules_used": False,
        "ios_boot_verified": False,
    }


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ["console", "runner", "contract", "pci-contract"]:
        p.add_argument("--" + name, required=True, type=Path)
    a = p.parse_args()
    print(
        json.dumps(
            assess(
                a.console.read_text(),
                a.runner.read_text(),
                json.loads(a.contract.read_text()),
                json.loads(a.pci_contract.read_text()),
            ),
            indent=2,
        )
    )
