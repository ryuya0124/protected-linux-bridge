#!/usr/bin/env python3
"""Analyze saved SurfaceFlinger latency; never change tracing/display settings.

AOSP FrameTracker::dumpStats writes desired, actual-present and ready timestamps.
Only finite actual-present timestamps count. Duplicate callbacks and unsignaled
fences must not become extra displayed frames. Overlapping snapshots establish
that polling did not skip the 127-frame history between samples.
"""

import argparse
import json
import math
import statistics
from pathlib import Path

PENDING = (1 << 63) - 1


def parse_latency(text):
    lines = text.strip().splitlines()
    if not lines or not lines[0].isdigit() or int(lines[0]) <= 0:
        raise ValueError("Missing positive display period")
    actual = set()
    for line in lines[1:]:
        if not line.strip():
            continue
        fields = line.split()
        if len(fields) != 3 or any(not item.isdigit() for item in fields):
            raise ValueError("Malformed SurfaceFlinger timestamp row")
        desired, presented, ready = map(int, fields)
        if any(value > PENDING for value in (desired, presented, ready)):
            raise ValueError("Timestamp outside signed 64-bit range")
        if 0 < presented < PENDING:
            actual.add(presented)
    return int(lines[0]), actual


def analyze(samples, layer):
    observations = [parse_latency(row["output"]) for row in samples if row["layer"] == layer]
    if len(observations) < 2 or any(not times for _, times in observations):
        raise ValueError("Need at least two populated snapshots of the selected layer")
    periods = sorted({period for period, _ in observations})
    histories = [times for _, times in observations]
    if any(max(current) < max(previous) for previous, current in zip(histories, histories[1:])):
        raise ValueError("Presentation history reset or regressed")
    overlaps = [len(previous & current) for previous, current in zip(histories, histories[1:])]
    complete = all(overlaps)
    # Exclude old boot/static content still present in the first ring. Measure
    # the advancing interval ending at the last snapshot, not callback count.
    start, end = max(histories[0]), max(histories[-1])
    times = sorted(value for value in set.union(*histories) if start <= value <= end)
    intervals = [b - a for a, b in zip(times, times[1:])]
    duration = end - start
    rate = (len(times) - 1) * 1e9 / duration if duration else None
    return {
        "source": "SurfaceFlinger actual-present timestamps, second column",
        "display_periods_ns": periods,
        "populated_snapshots": len(histories),
        "all_adjacent_histories_overlap": complete,
        "smallest_history_overlap": min(overlaps),
        "duration_seconds": duration / 1e9,
        "unique_presented_frames_after_initial_anchor": len(times) - 1,
        "presented_fps": rate if complete else None,
        "observed_minimum_rate_fps": rate,
        "median_interval_ms": statistics.median(intervals) / 1e6 if intervals else None,
        "p95_interval_ms": (
            sorted(intervals)[math.ceil(len(intervals) * 0.95) - 1] / 1e6 if intervals else None
        ),
        "longest_interval_ms": max(intervals) / 1e6 if intervals else None,
        "qualification": (
            "Only this bounded layer/workload interval; not input latency or long-term stability. "
            "Missing history overlap withholds presented_fps instead of treating lost rows as drops."
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samples", type=Path)
    parser.add_argument("--layer", required=True)
    args = parser.parse_args()
    print(json.dumps(analyze(json.loads(args.samples.read_text()), args.layer), indent=2))


if __name__ == "__main__":
    main()
