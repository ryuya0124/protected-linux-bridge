#!/usr/bin/env python3
"""Run one bounded diskless probe on an explicitly selected root-ADB Galaxy.

No host settings, mounts, partitions, unrelated VMs or persistent guest disks
are changed. Only the owned VMM's private mappings may be sampled.
"""

from pathlib import Path
import argparse
import json
import shlex
import subprocess
from datetime import datetime, timezone

FILES = "/data/user/0/net.ryuya_dev.protectedlinux/files"
KERNELS = {
    "linux618": FILES + "/kernels/linux-6.18.57-plb1/Image",
    "sdk37-16k": FILES + "/kernels/android37-sdk16k/Image",
    "cf17": FILES + "/kernels/android17-cuttlefish/Image",
    "stock": "/apex/com.android.virt/etc/fs/microdroid_kernel",
}


def assess_probe(exit_code, guest_log, vmm_log, touch):
    """Require successful readback and completion, even when crosvm exits zero."""
    stages = [line.strip() for line in guest_log.splitlines() if line.startswith("PLB_MEMORY_")]
    crash = any(
        message in vmm_log.lower()
        for message in ("vcpu crashed", "vcpu hit unknown error", "unknown gh exit reason")
    )
    expected = [
        f"PLB_MEMORY_TOUCHED mib={touch}",
        "PLB_MEMORY_READBACK errors=0",
        "PLB_MEMORY_COMPLETE",
    ]
    positions = [stages.index(item) if item in stages else -1 for item in expected]
    passed = (
        exit_code == 0
        and not crash
        and all(index >= 0 for index in positions)
        and positions == sorted(positions)
        and not any("FAILED" in line or "REJECTED" in line for line in stages)
    )
    return {"complete": passed, "vcpu_failure": crash, "stages": stages}


def host_snapshot(shell):
    """Read-only observations; missing optional statistics never change policy."""
    paths = {
        "boot_id": "/proc/sys/kernel/random/boot_id",
        "memory": "/proc/meminfo",
        "buddy": "/proc/buddyinfo",
        "shmem_thp": "/sys/kernel/mm/transparent_hugepage/shmem_enabled",
        "anonymous_thp": "/sys/kernel/mm/transparent_hugepage/enabled",
    }
    result = {}
    for label, path in paths.items():
        try:
            result[label] = shell("cat " + shlex.quote(path))
        except (subprocess.SubprocessError, OSError):
            result[label] = "unavailable"
    result["selinux"] = shell("getenforce")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="Explicit Galaxy adb serial")
    parser.add_argument("--adb", default="adb", help="ADB executable path")
    parser.add_argument("--server-port", type=int, default=5037, help="Local ADB server port")
    parser.add_argument("--kernel", choices=KERNELS, required=True)
    parser.add_argument("--memory", type=int, choices=(512, 768, 1024), required=True)
    parser.add_argument("--touch", type=int, required=True, help="Guest MiB to allocate and touch")
    parser.add_argument("--hugepages", action="store_true", help="VMM-local huge-page advice only")
    backing = parser.add_mutually_exclusive_group()
    backing.add_argument(
        "--anonymous-backing",
        action="store_true",
        help="Diskless research only: private anonymous guest RAM; no external backends",
    )
    backing.add_argument(
        "--prefault-backing",
        action="store_true",
        help="Diskless research: populate private anonymous RAM before guest donation",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 64 <= args.touch <= args.memory - 64:
        parser.error(
            "Touch size must be 64 MiB or more and leave at least 64 MiB to the guest kernel"
        )
    if not 1024 <= args.server_port <= 65535:
        parser.error("Invalid local ADB server port")
    adb = [args.adb, "-P", str(args.server_port), "-s", args.serial]

    def shell(command):
        return subprocess.check_output(adb + ["shell", command], text=True, timeout=15).strip()

    if shell("getprop ro.product.model") != "SM-S948Q" or shell("id -u") != "0":
        parser.error("Expected the explicitly selected Galaxy SM-S948Q with root adbd")
    info = shell("cat /proc/meminfo")
    available = int(
        next(line.split()[1] for line in info.splitlines() if line.startswith("MemAvailable:"))
    )
    if available < (args.memory + 768) * 1024:
        parser.error("Insufficient host memory headroom; host policies will not be changed")
    if args.anonymous_backing or args.prefault_backing:
        hashes = shell(
            "sha256sum /apex/com.android.virt/bin/crosvm " + FILES + "/kernel-probes/crosvm-copy"
        ).splitlines()
        if len(hashes) != 2 or hashes[0].split()[0] != hashes[1].split()[0]:
            parser.error("The private diagnostic crosvm copy must match the installed VMM exactly")
    args.output.mkdir(parents=True, exist_ok=True)
    label = f"{args.kernel}-{args.memory}-{args.touch}" + ("-huge" if args.hugepages else "")
    if args.anonymous_backing or args.prefault_backing:
        label += "-prefault" if args.prefault_backing else "-anonymous"
    output = args.output / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ-") + label)
    output.mkdir(mode=0o700)
    before = host_snapshot(shell)
    if before["selinux"] != "Enforcing":
        parser.error("Expected unchanged host SELinux Enforcing; refusing the probe")
    (output / "host-before.json").write_text(json.dumps(before, indent=2) + "\n")
    command = [
        FILES + "/kernel-probes/memory-runner",
        args.kernel,
        str(args.memory),
        str(args.touch),
        "1" if args.hugepages else "0",
        "2" if args.prefault_backing else "1" if args.anonymous_backing else "0",
    ]
    with (output / "vmm.log").open("w") as out:
        result = subprocess.run(
            adb + ["shell", shlex.join(command)], stdout=out, stderr=subprocess.STDOUT, timeout=35
        )
    log = FILES + "/kernel-probes/memory-live.log"
    guest_log = output / "guest.log"
    pull = subprocess.run(adb + ["pull", log, str(guest_log)], capture_output=True, text=True)
    data = guest_log.read_text(errors="replace") if pull.returncode == 0 else ""
    summary = {
        "kernel": args.kernel,
        "memory": args.memory,
        "touch": args.touch,
        "hugepages": args.hugepages,
        "anonymous_backing": args.anonymous_backing,
        "prefault_backing": args.prefault_backing,
        "exit": result.returncode,
        **assess_probe(result.returncode, data, (output / "vmm.log").read_text(), args.touch),
    }
    if (args.anonymous_backing or args.prefault_backing) and "PLB_BACKING_REPLACED" not in (
        output / "vmm.log"
    ).read_text():
        summary["complete"] = False
        summary["backing_not_applied"] = True
    if args.prefault_backing and "PLB_BACKING_PREFAULTED" not in (output / "vmm.log").read_text():
        summary["complete"] = False
        summary["prefault_not_applied"] = True
    after = host_snapshot(shell)
    (output / "host-after.json").write_text(json.dumps(after, indent=2) + "\n")
    summary["host_boundary_unchanged"] = (
        before["boot_id"] != "unavailable"
        and before["boot_id"] == after["boot_id"]
        and after["selinux"] == "Enforcing"
    )
    (output / "result.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))
    raise SystemExit(0 if summary["complete"] and summary["host_boundary_unchanged"] else 1)


if __name__ == "__main__":
    main()
