#!/usr/bin/env python3
"""Describe early XNU serial evidence without promoting a panic/hang to an iOS boot."""

import argparse
import json
from pathlib import Path
import re


def assess(console: str, runner: str, nonce: str) -> dict:
    if not re.fullmatch(r"[0-9a-f]{32}", nonce):
        raise ValueError("Invalid trial nonce")
    receipts = re.findall(
        r"PLB_NATIVE_XNU nonce=([0-9a-f]{32}) phase=([a-z_]+) status=(\d+)", console
    )
    if receipts[:2] != [(nonce, "uefi_entry", "0"), (nonce, "loader_ready", "0")]:
        raise ValueError("The native loader handoff was not established for this trial")
    if len(receipts) != 2:
        raise ValueError("Loader returned or ambiguous guest receipts were found")
    results = re.findall(r"PLB_NATIVE_XNU_RUNNER nonce=([0-9a-f]{32}) result=(\d+)", runner)
    if len(results) != 1 or results[0][0] != nonce:
        raise ValueError("Missing or ambiguous host supervisor result")
    result = int(results[0][1])
    entered = "Darwin Kernel Version 23.6.0:" in console and "kprintf initialized" in console
    panic = re.search(r"panic\(cpu \d+ caller [^)]+\): ([^\r\n]+)", console)
    return {
        "native_loader_handoff": True,
        "xnu_serial_execution_observed": entered,
        "vm_bootstrap_observed": entered and "VM bootstrap done:" in console,
        "guest_panic": panic.group(1) if panic else None,
        "host_supervisor_exit": result,
        "runtime_safety_stop": result == 88,
        "bsd_network_init_observed": entered and "Skywalk: running self-tests" in console,
        "waiting_for_root_media_observed": entered
        and "Waiting on <dict" in console
        and "Apple_HFS" in console,
        "crypto_post_all_passed_observed": entered
        and re.search(r"FIPSPOST_KEXT[^\r\n]*: all tests PASSED", console) is not None
        and "corecrypto_kext_start completed successfully" in console,
        "diagnostic_deadline_reached": result == 124,
        "normal_guest_shutdown_verified": False,
        "root_filesystem_boot_verified": False,
        "ios_boot_verified": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    args = parser.parse_args()
    contract = json.loads(args.contract.read_text())
    print(
        json.dumps(
            assess(args.console.read_text(), args.runner.read_text(), contract["nonce"]), indent=2
        )
    )
