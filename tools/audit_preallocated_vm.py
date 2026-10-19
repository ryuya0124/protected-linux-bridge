#!/usr/bin/env python3
"""Audit one bounded saved VM log offline; never connect to or start a device.

The VMM can return zero after a vCPU fault. Require positive guest readback and
completion, exact adoption identity/size, and both restorers for a policy trial.
Output omits raw logs, PFNs, addresses, host identities and local file paths.
"""

import argparse
import json
from pathlib import Path
import re

from run_memory_probe import assess_probe


def assess(text, memory, touch, policy=False, full_content=False):
    text = text.replace("\r", "")
    exits = re.findall(r"^PLB_PREALLOCATED_EXIT code=(\d+)$", text, re.MULTILINE)
    exit_code = int(exits[0]) if len(exits) == 1 else None
    guest = assess_probe(exit_code, text, text, touch)
    imports = re.findall(
        r"^PLB_RAM_ADOPT bytes=(\d+) identity=1 seals=(\d+) imported=1$", text, re.MULTILINE
    )
    adoption = len(imports) == 1 and int(imports[0][0]) == memory * 1024**2
    if adoption:
        seals = int(imports[0][1])
        adoption = seals in (7, 7 | 32)
    controller_restore = re.findall(
        r"^PLB_POLICY_RESTORED=(\d+) HOST_BOUNDARY_UNCHANGED=(\d+)$", text, re.MULTILINE
    )
    guardian_restore = re.findall(r"^PLB_POLICY_GUARDIAN_RESTORED=(\d+)$", text, re.MULTILINE)
    restored = not policy or (controller_restore == [("1", "1")] and guardian_restore == ["1"])
    remote_exits = re.findall(r"^PLB_REMOTE_EXIT (\d+)$", text, re.MULTILINE)
    outer_success = not remote_exits or remote_exits == ["0"]
    fault_errors = [
        int(value) for value in re.findall(r"PLB_GUNYAH_PAGE_FAULT attempt=(-?\d+)", text)
    ]
    blocks = {}
    current = None
    for line in text.splitlines():
        batch = re.fullmatch(r"PLB_PREALLOCATED_BATCH offset_mib=(\d+) bytes=(\d+)", line)
        if batch:
            current = int(batch[1])
        pages = re.fullmatch(
            r"PLB_PREALLOCATED_PAGES sampled=(\d+) thp=(\d+) compound_heads=(\d+) expected=(\d+)",
            line,
        )
        if pages:
            # A pin test samples before/after; retain the final observation of
            # each block instead of double-counting the same physical backing.
            blocks[current if current is not None else 0] = tuple(map(int, pages.groups()))
    sampled = sum(row[0] for row in blocks.values()) if blocks else None
    thp = sum(row[1] for row in blocks.values()) if blocks else None
    heads = sum(row[2] for row in blocks.values()) if blocks else None
    all_words = re.findall(
        r"^PLB_MEMORY_FULL_CONTENT checked_bytes=(\d+) errors=(\d+)$", text, re.MULTILINE
    )
    all_words_verified = len(all_words) == 1 and tuple(map(int, all_words[0])) == (
        touch * 1024**2,
        0,
    )
    complete = bool(
        guest["complete"]
        and adoption
        and restored
        and outer_success
        and "HOST_BOUNDARY_UNCHANGED=0" not in text
        and not fault_errors
        and "PLB_PREALLOCATED_HOST_FAILURE" not in text
        and (not full_content or all_words_verified)
    )
    return {
        "memory_mib": memory,
        "requested_readback_mib": touch,
        "full_readback_complete": complete,
        "verified_readback_mib": touch if complete else None,
        "readback_kind": "all_64_bit_words" if full_content else "page_markers",
        "all_bytes_verified": complete and all_words_verified,
        "max_write_progress_mib": max(
            [int(value) for value in re.findall(r"PLB_MEMORY_PROGRESS round=1 mib=(\d+)", text)],
            default=0,
        ),
        "native_exit_code": exit_code,
        "outer_exit_code": int(remote_exits[0]) if len(remote_exits) == 1 else None,
        "exact_fd_adoption": adoption,
        "required_policy_restoration_observed": restored,
        "linux_page_fault_attempt_errors": fault_errors,
        "vcpu_failure": guest["vcpu_failure"],
        "last_backing_sampled_pages": sampled,
        "last_backing_thp_pages": thp,
        "last_backing_compound_heads": heads,
        "backing_snapshot_is_complete": sampled == memory * 256 if blocks else None,
        "notes": "Write progress and a zero VMM exit are not successful readback.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--memory", type=int, required=True)
    parser.add_argument("--touch", type=int, required=True)
    parser.add_argument("--policy", action="store_true")
    parser.add_argument("--full-content", action="store_true")
    args = parser.parse_args()
    if args.log.stat().st_size > 8 * 1024**2:
        parser.error("Saved log exceeds 8 MiB")
    print(
        json.dumps(
            assess(args.log.read_text(), args.memory, args.touch, args.policy, args.full_content),
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
