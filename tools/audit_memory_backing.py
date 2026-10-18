#!/usr/bin/env python3
"""Read-only Galaxy memory-backing audit; never starts a VM or changes policy.

Raw snapshots stay in an explicitly selected local directory. The summary contains
no serial, boot ID, physical address or process map. Missing data stays unknown.
"""

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import shlex
import subprocess

THP = "/sys/kernel/mm/transparent_hugepage"
READS = {
    "model": "getprop ro.product.model",
    "uid": "id -u",
    "kernel": "uname -r",
    "page_size": "getconf PAGESIZE",
    "boot_id": "cat /proc/sys/kernel/random/boot_id",
    "selinux": "getenforce",
    "meminfo": "cat /proc/meminfo",
    "buddyinfo": "cat /proc/buddyinfo",
    "vmstat": "cat /proc/vmstat",
    "anonymous_thp": f"cat {THP}/enabled",
    "shmem_thp": f"cat {THP}/shmem_enabled",
    "defrag": f"cat {THP}/defrag",
    "reserve_module": (
        "if [ -d /sys/module/gh_hugepage_reserve ]; then printf present; else printf absent; fi"
    ),
    "kernel_config": (
        "zcat /proc/config.gz | grep -E "
        "'^(# )?CONFIG_(ARM64_4K_PAGES|TRANSPARENT_HUGEPAGE|TRANSPARENT_HUGEPAGE_ALWAYS|"
        "TRANSPARENT_HUGEPAGE_MADVISE|COMPACTION|HUGETLBFS|HUGETLB_PAGE|"
        "KPROBES|KRETPROBES|KALLSYMS|IO_URING|CMA|TMPFS)(=| is not set)'"
    ),
}
# Read both policies: anonymous 'enabled' and shared-memory 'shmem_enabled' are
# separate controls on kernels exposing both. Do not infer one from the other.
for size in (16, 32, 64, 128, 256, 512, 1024, 2048):
    for policy in ("enabled", "shmem_enabled"):
        READS[f"thp_{size}k_{policy}"] = f"cat {THP}/hugepages-{size}kB/{policy}"

BOUNDARY = ("boot_id", "selinux", "anonymous_thp", "shmem_thp", "defrag") + tuple(
    key for key in READS if key.startswith("thp_")
)


def selected_policy(text):
    """Kernel sysfs lists bracket the selected policy; do not guess from options."""
    match = re.fullmatch(r"[a-z_+ ]*\[([a-z_+]+)\][a-z_+ ]*", text.strip())
    return match.group(1) if match else None


def parse_meminfo(text):
    return {
        match[1]: int(match[2])
        for line in text.splitlines()
        if (match := re.fullmatch(r"([A-Za-z_()]+):\s+(\d+) kB", line.strip()))
    }


def buddy_summary(text, page_size):
    """Count only existing free buddy blocks; these are neither reserved nor CMA-safe."""
    huge = 2 * 1024 * 1024
    if page_size <= 0 or page_size > huge or page_size & (page_size - 1):
        return None
    ratio = huge // page_size
    order = int(math.log2(ratio))
    zones = []
    for line in text.splitlines():
        match = re.fullmatch(r"Node\s+(\d+),\s+zone\s+(\S+)\s+([0-9\s]+)", line.strip())
        if not match:
            continue
        counts = [int(value) for value in match[3].split()]
        if len(counts) <= order:
            continue
        units = sum(
            count << (index - order) for index, count in enumerate(counts) if index >= order
        )
        zones.append({"node": int(match[1]), "zone": match[2], "free_2m_equivalents": units})
    if not zones:
        return None
    return {
        "order_for_2m": order,
        "zones": zones,
        "free_2m_equivalents": sum(zone["free_2m_equivalents"] for zone in zones),
        "capacity_guaranteed": False,
    }


def classify_failure(text):
    """Separate the measured failing stage from theories about its allocator."""
    stages = []
    if re.search(r"Failed to map guest address[^\n]*:\s*10\b", text):
        stages.append("gunyah_addrspace_map_nomem")
    if re.search(r"Failed to donate memory[^\n]*:\s*10\b", text):
        stages.append("gunyah_memextent_donate_nomem")
    if re.search(r"Failed to provide folio[^\n]*:\s*-12\b", text):
        stages.append("folio_provision_enomem")
    if "Kernel panic - not syncing: Out of memory" in text:
        stages.append("linux_oom_panic_context_required")
    return {
        "observed_stages": stages,
        "exact_exhausted_resource": "unproven",
    }


def summarize(snapshot, target_mib=4096):
    values = snapshot["reads"]
    memory = parse_meminfo(values.get("meminfo", ""))
    page = values.get("page_size", "")
    buddy = buddy_summary(values.get("buddyinfo", ""), int(page) if page.isdigit() else 0)
    available = memory.get("MemAvailable")
    # Same minimum headroom as the existing Bridge manager. This is an admission
    # check, not proof that Gunyah can map or that Android won't reclaim memory.
    minimum = (target_mib + 768) * 1024
    after = snapshot.get("boundary_after", {})
    boundary_known = all(values.get(key) and after.get(key) for key in BOUNDARY)
    boundary_unchanged = (
        all(values.get(key) == after.get(key) for key in BOUNDARY)
        if boundary_known and after
        else None
    )
    return {
        "target_mib": target_mib,
        "minimum_headroom_kib": minimum,
        "mem_available_kib": available,
        "passes_existing_headroom_check": available >= minimum if available is not None else None,
        "shmem_policy": selected_policy(values.get("shmem_thp", "")),
        "anonymous_policy": selected_policy(values.get("anonymous_thp", "")),
        "defrag_policy": selected_policy(values.get("defrag", "")),
        "buddy": buddy,
        "reserve_module": values.get("reserve_module") or "unknown",
        "boundary_unchanged": boundary_unchanged,
        "host_selinux_enforcing": (
            values.get("selinux") == "Enforcing" if values.get("selinux") else None
        ),
        "huge_folios_verified_for_vm": False,
        "gunyah_internal_cause": "unproven",
        "read_errors": sorted(snapshot.get("errors", {})),
    }


def collect(shell):
    reads = {}
    errors = {}

    def read(key):
        try:
            return shell(READS[key]).strip()
        except (OSError, subprocess.SubprocessError) as error:
            errors[key] = type(error).__name__
            return ""

    # Explicit target/root identity before touching even optional diagnostics.
    for key in ("model", "uid"):
        reads[key] = read(key)
    if reads["model"] != "SM-S948Q" or reads["uid"] != "0":
        raise ValueError("Expected the explicitly selected root-ADB Galaxy SM-S948Q")
    for key in READS:
        if key not in reads:
            reads[key] = read(key)
    after = {key: read(key) for key in BOUNDARY}
    return {
        "schema": 1,
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "reads": reads,
        "boundary_after": after,
        "errors": errors,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--serial", help="Explicit Galaxy ADB serial; never auto-select a device")
    source.add_argument(
        "--snapshot", type=Path, help="Analyze an existing private JSON snapshot offline"
    )
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--server-port", type=int, default=5037)
    parser.add_argument(
        "--su", action="store_true", help="Use already-permitted su for fixed read commands"
    )
    parser.add_argument(
        "--target-mib", type=int, choices=(512, 768, 1024, 2048, 4096), default=4096
    )
    parser.add_argument(
        "--output", type=Path, required=True, help="New private local output directory"
    )
    parser.add_argument(
        "--failure-log", type=Path, help="Optional existing private log; analyze offline only"
    )
    args = parser.parse_args()
    if not 1024 <= args.server_port <= 65535:
        parser.error("Invalid local ADB server port")
    if args.snapshot:
        snapshot = json.loads(args.snapshot.read_text())
    else:
        adb = [args.adb, "-P", str(args.server_port), "-s", args.serial]

        def shell(command):
            # All remote commands come from the fixed read-only table above.
            if args.su:
                command = "su -c " + shlex.quote(command)
            return subprocess.check_output(
                adb + ["shell", command], text=True, stderr=subprocess.PIPE, timeout=10
            )

        snapshot = collect(shell)
    result = summarize(snapshot, args.target_mib)
    if args.failure_log:
        if args.failure_log.stat().st_size > 2 * 1024 * 1024:
            parser.error("Select a log no larger than 2 MiB")
        result["failure"] = classify_failure(args.failure_log.read_text(errors="replace"))
    # Never replace an earlier measurement. Private raw input may contain boot ID.
    args.output.mkdir(mode=0o700, parents=True, exist_ok=False)
    for name, content in (("snapshot.json", snapshot), ("summary.json", result)):
        path = args.output / name
        path.write_text(json.dumps(content, indent=2) + "\n")
        path.chmod(0o600)
    print(json.dumps(result, indent=2))
    if result["boundary_unchanged"] is not True or result["host_selinux_enforcing"] is not True:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
