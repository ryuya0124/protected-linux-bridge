#!/usr/bin/env python3
"""Qualify the actual mounted-root/provider audit without enabling acceptance."""

import argparse
import json
from pathlib import Path

from native_xnu_verified_result import assess as assess_verified, RECEIPTS

BINDING_PHASES = [
    "root_binding_device",
    "root_binding_matches",
    "root_binding_checks",
    "root_binding_negative",
    "root_binding_audit",
]


def assess(console: str, runner: str, contract: dict, pattern: dict) -> dict:
    if (
        contract.get("root_binding_audit") is not True
        or pattern.get("root_binding_audit") is not True
        or contract.get("inject_block_fault") is not False
    ):
        raise ValueError("Not an explicit unmodified-data root-binding audit")
    rows = RECEIPTS.findall(console)
    if any(row[0] != contract["nonce"] for row in rows):
        raise ValueError("Mixed mounted-root receipt nonces")
    binding = [(phase, int(a), int(b)) for _, phase, a, b in rows if phase in BINDING_PHASES]
    if [row[0] for row in binding] != BINDING_PHASES:
        raise ValueError("Missing, duplicate or reordered mounted-root audit evidence")
    major, minor = binding[0][1:]
    if not 0 <= major <= 255 or not 0 <= minor <= 0xFFFFFF:
        raise ValueError("Invalid actual mounted BSD device")
    matches, depth = binding[1][1:]
    if (
        matches != 1
        or not 1 <= depth <= 16
        or binding[2:]
        != [
            ("root_binding_checks", 127, 127),
            ("root_binding_negative", 7, 7),
            ("root_binding_audit", 1, 0),
        ]
    ):
        raise ValueError("Root/provider/integrity binding failed or acceptance was enabled")
    phases = [row[1] for row in rows]
    start = phases.index(BINDING_PHASES[0])
    if (
        start == 0
        or phases[start - 1] != "apple_root_auth_result"
        or phases[start : start + 5] != BINDING_PHASES
        or phases[start + 5 : start + 7] != ["integrity_stats", "integrity_failures"]
    ):
        raise ValueError("Binding was not measured after actual root auth, before device teardown")
    stripped = (
        "\n".join(
            line
            for line in console.splitlines()
            if not (match := RECEIPTS.search(line)) or match.group(2) not in BINDING_PHASES
        )
        + "\n"
    )
    # Normalize this one flag only after checking every ordered audit receipt.
    # The plain read verifier must not qualify an unmeasured binding profile.
    result = assess_verified(
        stripped,
        runner,
        dict(contract, root_binding_audit=False),
        dict(pattern, root_binding_audit=False),
    )
    if not result["filesystem_mount_verified"] or result["root_authentication_errno"] != 80:
        raise ValueError("The pinned real read-only main-root refusal was not preserved")
    result.update(
        mounted_root_binding_audit_verified=True,
        mounted_bsd_major=major,
        mounted_bsd_minor=minor,
        provider_depth=depth,
        root_binding_negative_controls_verified=True,
    )
    return result


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
