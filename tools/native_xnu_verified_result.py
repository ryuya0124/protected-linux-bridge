#!/usr/bin/env python3
"""Separate real per-read integrity evidence from Apple root authentication/boot."""

import argparse
import json
from pathlib import Path
import re

from build_native_ios_block_policy import TABLE_SHA256, TABLE_BYTES, CHUNK_BYTES
from native_ios_root_contract import RAW_BYTES, RAW_SHA256
from native_xnu_original_result import assess as assess_original
from native_xnu_result import assess as assess_kernel

RECEIPTS = re.compile(
    r"PLB_XNU_ORIGINAL nonce=([0-9a-f]{32}) phase=([a-z][a-z0-9_]*) a=(\d+) b=(\d+)"
)
POLICY_PHASES = {
    "sha256_kat",
    "integrity_policy_ready",
    "integrity_fault_injected",
    "integrity_rejected",
    "integrity_stats",
    "integrity_failures",
}


def assess(console: str, runner: str, contract: dict, pattern: dict) -> dict:
    if contract.get("root_binding_audit", False) or pattern.get("root_binding_audit", False):
        raise ValueError("Use the separate mounted-root binding assessor for this profile")
    nonce = contract["nonce"]
    policy = pattern.get("block_policy", {})
    if (
        contract.get("block_verification") is not True
        or contract.get("owner_root_policy_enabled") is not False
        or contract.get("source_storage_archive_linked") is not False
        or contract.get("fixture_kind") != "official_main_os"
        or policy.get("table_sha256") != TABLE_SHA256
        or policy.get("table_bytes") != TABLE_BYTES
        or policy.get("image_sha256") != RAW_SHA256
        or policy.get("image_bytes") != RAW_BYTES
        or policy.get("chunk_bytes") != CHUNK_BYTES
        or pattern.get("nonce") != nonce
        or pattern.get("disk_sha256") != RAW_SHA256
    ):
        raise ValueError("Not the exact separately qualified block-verification profile")
    negative = contract.get("inject_block_fault")
    if type(negative) is not bool or pattern.get("inject_block_fault") is not negative:
        raise ValueError("Fault mode was not explicitly pinned in both trial manifests")
    rows = RECEIPTS.findall(console)
    if any(row[0] != nonce for row in rows):
        raise ValueError("Mixed integrity receipt nonces")
    integrity = [(phase, int(a), int(b)) for _, phase, a, b in rows if phase in POLICY_PHASES]
    expected = ["sha256_kat", "integrity_policy_ready"]
    if negative:
        expected += ["integrity_fault_injected", "integrity_rejected"]
    expected += ["integrity_stats", "integrity_failures"]
    if [row[0] for row in integrity] != expected or integrity[:2] != [
        ("sha256_kat", 1, 256),
        ("integrity_policy_ready", CHUNK_BYTES, TABLE_BYTES // 32),
    ]:
        raise ValueError("Incomplete, ambiguous or invalid integrity policy/KAT evidence")
    if any(a >= 2**64 or b >= 2**64 for _, a, b in integrity):
        raise ValueError("Invalid integrity counter range")
    if not negative:
        _, chunks, pages = integrity[-2]
        if not 2 <= chunks <= pages or integrity[-1] != ("integrity_failures", 0, 0):
            raise ValueError("Unqualified reads or an integrity failure occurred")
        stripped = (
            "\n".join(
                line
                for line in console.splitlines()
                if not (match := RECEIPTS.search(line)) or match.group(2) not in POLICY_PHASES
            )
            + "\n"
        )
        result = assess_original(stripped, runner, contract, pattern)
        phases = [row[1] for row in rows]
        if not (
            phases.index("integrity_policy_ready")
            < phases.index("block_started")
            < phases.index("apple_root_auth_result")
            < phases.index("integrity_stats")
            < phases.index("device_reset")
        ):
            raise ValueError("Integrity evidence is outside the actual storage/root lifecycle")
        result.update(
            consumed_read_verification_observed=True,
            authenticated_chunk_loads=chunks,
            authenticated_page_deliveries=pages,
            counts_are_unique_coverage=False,
            tamper_refusal_observed=False,
            owner_root_policy_enabled=False,
        )
        return result
    if integrity[2][2] != 1 or integrity[2][1] >= RAW_BYTES or integrity[2][1] & 4095:
        raise ValueError("Invalid negative private-snapshot fault receipt")
    if integrity[3:] != [
        ("integrity_rejected", 0, 0),
        ("integrity_stats", 0, 0),
        ("integrity_failures", 1, 1),
    ]:
        raise ValueError("Tamper control delivered bytes or failed to poison its read path")
    if re.findall(r"PLB_NATIVE_XNU_RUNNER nonce=([0-9a-f]{32}) result=(\d+)", runner) != [
        (nonce, "0")
    ]:
        raise ValueError("Negative control supervisor did not finish normally")
    firmware = contract.get("firmware_nonce", nonce)
    kernel = assess_kernel(console, f"PLB_NATIVE_XNU_RUNNER nonce={firmware} result=0\n", firmware)
    if (
        not kernel["xnu_serial_execution_observed"]
        or not kernel["crypto_post_all_passed_observed"]
        or kernel["guest_panic"] is not None
    ):
        raise ValueError("Negative control did not reach the real kernel/crypto path")
    phases = [row[1] for row in rows]
    prohibited = {
        "root_selected",
        "vfs_mount_result",
        "apple_root_auth_result",
        "pass",
        "bsd_gpt_prefix_pass",
        "bsd_partial_tail_pass",
    }
    if prohibited.intersection(phases) or phases[-3:] != [
        "device_reset",
        "failed",
        "guest_shutdown",
    ]:
        raise ValueError("Tampered data was accepted or the device was not reset/shut down")
    if ("base_class_size", 144, 144) not in [(p, int(a), int(b)) for _, p, a, b in rows]:
        raise ValueError("Original storage base class was not qualified")
    media = re.findall(
        r"PLB_XNU_MEDIA nonce=([0-9a-f]{32}) phase=([a-z_]+) a=(\d+) b=(\d+)", console
    )
    if (
        len(media) != 3
        or any(row[0] != nonce for row in media)
        or [row[1] for row in media] != ["candidate", "candidate", "queue_ready"]
        or {int(media[0][3]), int(media[1][3])} != {131072, RAW_BYTES // 512}
        or media[0][2] == media[1][2]
        or any(not 0 <= int(row[2]) < 256 for row in media[:2])
        or tuple(map(int, media[2][2:])) != (0x9FFF0000, 8192)
    ):
        raise ValueError("Negative control did not use the qualified read-only PCI/DMA path")
    return {
        "consumed_read_verification_observed": False,
        "authenticated_chunk_loads": 0,
        "authenticated_page_deliveries": 0,
        "tamper_refusal_observed": True,
        "filesystem_mount_verified": False,
        "root_authentication_evaluated": False,
        "owner_root_policy_enabled": False,
        "guest_shutdown_verified": True,
        "supervisor_exit": 0,
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
