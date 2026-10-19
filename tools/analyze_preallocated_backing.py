#!/usr/bin/env python3
"""Compare retained RAM preparation logs offline. No device access or allocation.

THP coverage, observed head density and successful VM workloads are distinct.
Head observations are not Gunyah mapping counts or a stability admission rule.
Repeated snapshots of a batch (for example around pinning) count only once.
"""

import argparse
import json
from pathlib import Path
import re

MIB = 1024**2
PAGE = 4096
BATCH = re.compile(r"PLB_PREALLOCATED_BATCH offset_mib=(\d+) bytes=(\d+)")
PAGES = re.compile(
    r"PLB_PREALLOCATED_PAGES sampled=(\d+) thp=(\d+) compound_heads=(\d+) expected=(\d+)"
)
READY = re.compile(r"PLB_PREALLOCATED_READY memory_mib=(\d+)\b")


def analyze(text):
    batches = {}
    current = None
    memory = None
    ready_count = 0
    for line in text.splitlines():
        if match := BATCH.search(line):
            offset, size = map(int, match.groups())
            current = offset * MIB
            if current in batches or size <= 0 or size % PAGE:
                raise ValueError("Duplicate batch offset or invalid batch size; split the log")
            batches[current] = {"bytes": size, "observation": None, "snapshots": 0}
        elif match := PAGES.search(line):
            if current is None:
                raise ValueError("Page observation without a batch")
            sampled, thp, heads, expected = map(int, match.groups())
            if not (0 <= heads <= sampled <= expected and 0 <= thp <= sampled):
                raise ValueError("Inconsistent page counters")
            batch = batches[current]
            if expected * PAGE != batch["bytes"]:
                raise ValueError("Page count does not match the batch size")
            batch["observation"] = (sampled, thp, heads, expected)
            batch["snapshots"] += 1
        elif match := READY.search(line):
            ready_count += 1
            memory = int(match[1])
    if ready_count != 1:
        raise ValueError("Exactly one READY record is required; do not merge trial logs")

    cursor = 0
    sampled = thp = heads = snapshots = 0
    complete = bool(batches)
    for offset, batch in sorted(batches.items()):
        complete &= offset == cursor
        cursor = offset + batch["bytes"]
        snapshots += batch["snapshots"]
        if batch["observation"] is None:
            complete = False
            continue
        observed, huge, head_count, expected = batch["observation"]
        complete &= observed == expected
        sampled += observed
        thp += huge
        heads += head_count
    complete &= cursor == memory * MIB and sampled * PAGE == memory * MIB
    density = heads / (sampled * PAGE / 1024**3) if sampled else None
    return {
        "configured_memory_mib": memory,
        "batches": len(batches),
        "snapshots_seen": snapshots,
        "complete_preparation_snapshot": bool(complete),
        "sampled_pages": sampled,
        "thp_pages": thp,
        "thp_coverage_percent": 100 * thp / sampled if sampled else None,
        "compound_head_observations": heads,
        "observed_heads_per_gib": density,
        "thp_kib_per_observed_head": thp * PAGE / 1024 / heads if heads else None,
        "source_has_full_memory_complete_marker": "PLB_MEMORY_COMPLETE" in text,
        "source_has_gunyah_fault_marker": bool(
            re.search(r"PAGE_FAULT.*(?:attempt=-12|guest_error=1)", text)
        ),
        "stability_validated_by_this_analysis": False,
        "gunyah_mapping_count": None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", type=Path, nargs="+")
    args = parser.parse_args()
    # Do not publish caller-specific paths or raw log excerpts in this report.
    reports = [analyze(path.read_text(errors="replace")) for path in args.logs]
    print(json.dumps(reports, indent=2) + "\n", end="")


if __name__ == "__main__":
    main()
