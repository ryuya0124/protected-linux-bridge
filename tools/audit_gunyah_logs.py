#!/usr/bin/env python3
"""Read one saved log without device access; keep failure stages and error domains separate.

Output contains only fixed categories, integer error counts and kernel uptimes.
No raw addresses, PFNs, process IDs, file paths or log text are emitted. This is
not an allocator probe, health certificate, automatic recovery or VM launcher.
"""

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import stat

MAX_BYTES = 32 * 1024 * 1024
KERNEL_TIME = re.compile(r"^\[\s*(\d+(?:\.\d+)?)\]")
ERRORS = {
    "address_map_hypervisor": re.compile(r"Failed to map guest address\b[^:\n]*:\s*(-?\d+)\b"),
    "donate_hypervisor": re.compile(
        r"Failed to donate memory for guest address\b[^:\n]*:\s*(-?\d+)\b"
    ),
    "provide_folio_linux": re.compile(r"Failed to provide folio\b[^\n]*:\s*(-?\d+)\b"),
    "reclaim_donation_hypervisor": re.compile(
        r"Failed to reclaim memory donation for guest address\b[^:\n]*:\s*(-?\d+)\b"
    ),
    "unmap_hypervisor": re.compile(r"Failed to unmap guest address\b[^:\n]*:\s*(-?\d+)\b"),
    "resource_manager": re.compile(r"RM rejected message\b[^\n]*Error:\s*(-?\d+)\b"),
}


def audit(text):
    errors = {name: Counter() for name in ERRORS}
    timed_bad_pages = {}
    untimed_bad_pages = 0
    prefault_refusals = 0
    generic_os_enomem = 0
    linux_oom = 0
    reclaim_warnings = 0
    seen_kernel_records = set()
    for line in text.splitlines():
        time = KERNEL_TIME.match(line)
        if time:
            # A repeated kernel snapshot is not a new fault. Never deduplicate
            # different boots together: this API intentionally accepts one log.
            if line in seen_kernel_records:
                continue
            seen_kernel_records.add(line)
        for name, pattern in ERRORS.items():
            match = pattern.search(line)
            if match:
                errors[name][str(int(match.group(1)))] += 1
        if "BUG: Bad page cache" in line:
            if time:
                timed_bad_pages[line] = float(time.group(1))
            else:
                untimed_bad_pages += 1
        prefault_refusals += "ANDROID_BACKING_UNSUITABLE" in line
        generic_os_enomem += "Out of memory (os error 12)" in line
        linux_oom += "Out of memory and no killable processes" in line
        reclaim_warnings += "Gunyah failed to reclaim" in line

    bursts = []
    for uptime in sorted(timed_bad_pages.values()):
        if not bursts or uptime - bursts[-1]["last_uptime_seconds"] > 1.0:
            bursts.append(
                {"first_uptime_seconds": uptime, "last_uptime_seconds": uptime, "reports": 0}
            )
        bursts[-1]["last_uptime_seconds"] = uptime
        bursts[-1]["reports"] += 1
    return {
        "schema": 1,
        "errors_by_stage_and_domain": {name: dict(counts) for name, counts in errors.items()},
        "address_map_nomem_observed": bool(errors["address_map_hypervisor"]["10"]),
        "distinct_timed_bad_page_records": len(timed_bad_pages),
        "untimed_bad_page_lines": untimed_bad_pages,
        "bad_page_bursts": bursts,
        "prefault_admission_refusal_lines": prefault_refusals,
        "generic_os_enomem_lines": generic_os_enomem,
        # The text alone cannot tell whether a Linux OOM came from host or guest.
        "linux_oom_lines_scope_unspecified": linux_oom,
        "reclaim_warning_lines": reclaim_warnings,
        "exact_hypervisor_allocator_identified": False,
        "vm_success_inferred_from_exit_status": False,
    }


def read_log(path):
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    with os.fdopen(fd, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_BYTES:
            raise ValueError("Require one regular saved log of at most 32 MiB")
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError("Saved log exceeded 32 MiB while reading")
    return data.decode("utf-8", errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    try:
        result = audit(read_log(args.log))
    except (OSError, ValueError):
        # Avoid echoing an untrusted/private path as a diagnostic.
        parser.exit(2, "Cannot read one bounded regular saved log\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
