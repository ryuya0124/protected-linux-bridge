#!/usr/bin/env python3
"""Summarize saved host memory metadata. Never allocate memory or write host policy."""

import argparse
import json
import re
from pathlib import Path

SIZES_KIB = (16, 32, 64, 128, 256, 512, 2048)


def summarize(buddyinfo, meminfo, page_kib=4):
    if page_kib != 4:
        raise ValueError("Only the verified 4 KiB host page size is supported")
    memory = {}
    for name in ("MemFree", "MemAvailable", "Cached", "SReclaimable", "Unevictable", "Mlocked"):
        match = re.search(rf"^{name}:\s+(\d+) kB$", meminfo, re.MULTILINE)
        if not match:
            raise ValueError(f"Missing {name} in meminfo")
        memory[name + "_kib"] = int(match[1])
    zones = []
    seen = set()
    for line in buddyinfo.splitlines():
        match = re.fullmatch(r"Node\s+(\d+),\s+zone\s+(\w+)\s+([\d\s]+)", line.strip())
        if not match:
            raise ValueError("Malformed buddyinfo line")
        node, name = int(match[1]), match[2]
        counts = [int(value) for value in match[3].split()]
        if not counts or len(counts) > 32 or (node, name) in seen:
            raise ValueError("Invalid or repeated buddy zone")
        seen.add((node, name))
        free_kib = sum(count * (page_kib << order) for order, count in enumerate(counts))
        eligible = {}
        for size in SIZES_KIB:
            minimum_order = (size // page_kib).bit_length() - 1
            blocks = sum(
                count << (order - minimum_order)
                for order, count in enumerate(counts)
                if order >= minimum_order
            )
            eligible[str(size)] = {
                "free_kib_in_blocks_at_least_this_size": blocks * size,
                "maximum_blocks_if_split": blocks,
            }
        zones.append(
            {
                "node": node,
                "zone": name,
                "free_kib": free_kib,
                "orders": counts,
                "eligible_by_size_kib": eligible,
            }
        )
    if not zones:
        raise ValueError("Empty buddyinfo")
    return {
        "page_kib": page_kib,
        "memory": memory,
        "zones": zones,
        "interpretation": (
            "Current free buddy blocks only; excludes potentially reclaimable/movable pages. "
            "Splitting upper bounds are not allocation guarantees and overlap across sizes. "
            "This is neither a fragmentation percentage nor a Gunyah resource-limit measurement."
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--buddyinfo", type=Path, required=True)
    parser.add_argument("--meminfo", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(summarize(args.buddyinfo.read_text(), args.meminfo.read_text()), indent=2))


if __name__ == "__main__":
    main()
