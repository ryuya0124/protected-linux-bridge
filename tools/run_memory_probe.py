#!/usr/bin/env python3
"""Run one bounded diskless probe on an explicitly selected root-ADB Galaxy.

Default mode changes no host settings, mounts, partitions, unrelated VMs or
persistent guest disks. The separately authorized --approved-small-shmem-policy
mode temporarily changes only the three documented host-wide shmem controls,
under an independent restoration guardian. Owned VMM mappings may be sampled.
"""

from pathlib import Path
import argparse
import json
import re
import shlex
import struct
import subprocess
from datetime import datetime, timezone

FILES = "/data/user/0/net.ryuya_dev.protectedlinux/files"
KERNELS = {
    "linux618": FILES + "/kernels/linux-6.18.57-plb1/Image",
    "sdk37-16k": FILES + "/kernels/android37-sdk16k/Image",
    "cf17": FILES + "/kernels/android17-cuttlefish/Image",
    "stock": "/apex/com.android.virt/etc/fs/microdroid_kernel",
}


def valid_fdt_dump(data):
    """Check the captured v17 envelope; semantic inspection still uses libfdt."""
    if not 40 <= len(data) < 1048576:
        return False
    (
        magic,
        total,
        structure,
        strings,
        reserve,
        version,
        compatible,
        _,
        strings_size,
        structure_size,
    ) = struct.unpack_from(">10I", data)
    return (
        magic == 0xD00DFEED
        and total == len(data)
        and version == 17
        and compatible <= version
        and reserve >= 40
        and reserve % 8 == 0
        and reserve + 16 <= total
        and structure >= 40
        and structure % 4 == 0
        and structure_size >= 4
        and structure_size % 4 == 0
        and structure + structure_size <= total
        and strings >= 40
        and strings + strings_size <= total
    )


def assess_probe(exit_code, guest_log, vmm_log, touch, rounds=1, discard=False):
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
    round_results = re.findall(
        r"^PLB_MEMORY_ROUND round=(\d+) errors=(\d+) minor_faults=(\d+) "
        r"major_faults=(\d+) maxrss_kib=(\d+)$",
        "\n".join(stages),
        re.MULTILINE,
    )
    # Legacy one-pass logs remain readable. Repeated probes require every
    # requested round in order, never just the final completion marker.
    if round_results:
        rounds_valid = [int(item[0]) for item in round_results] == list(
            range(1, rounds + 1)
        ) and all(int(item[1]) == 0 for item in round_results)
    else:
        rounds_valid = rounds == 1
    discard_markers = re.findall(
        r"^PLB_MEMORY_DISCARD round=(\d+) resident_pages=(\d+)$",
        "\n".join(stages),
        re.MULTILINE,
    )
    discard_valid = not discard_markers
    if discard:
        discard_valid = [int(item[0]) for item in discard_markers] == list(
            range(1, rounds)
        ) and all(int(item[1]) == 0 for item in discard_markers)
    passed = (
        exit_code == 0
        and not crash
        and rounds_valid
        and discard_valid
        and all(index >= 0 for index in positions)
        and positions == sorted(positions)
        and not any("FAILED" in line or "REJECTED" in line for line in stages)
    )
    return {"complete": passed, "vcpu_failure": crash, "stages": stages}


def workload_budget_mib(
    memory, touch, rounds, private_backing=False, hugepages=False, discard=False, prepared=False
):
    """Bound diagnostic coverage; large address reservations never imply residency."""
    if memory not in (512, 768, 1024, 2048, 4096, 5120) or not 64 <= touch <= memory - 64:
        raise ValueError("Unsupported memory size or insufficient guest-kernel allowance")
    if prepared:
        if (
            (memory, touch)
            not in ((512, 256), (1024, 768), (2048, 1536), (4096, 3072), (5120, 4096))
            or rounds != 1
            or discard
            or not hugepages
            or not private_backing
        ):
            raise ValueError("Unsupported prepared one-pass workload")
        return memory
    if memory in (2048, 5120):
        raise ValueError("This large working set requires the approved prepared stage")
    if not 1 <= rounds <= 8 or touch * rounds > 3072:
        raise ValueError("Repeated page coverage is bounded to 3072 MiB for the VM watchdog")
    if discard and rounds < 2:
        raise ValueError("Discard comparison requires at least two rounds")
    if memory == 4096:
        if touch > 256 or private_backing or hugepages:
            raise ValueError(
                "4 GiB is a lazy-reservation probe only: shared backing, touch <= 256 MiB"
            )
        # Include 512 MiB for guest boot allocations. An admission heuristic,
        # not a residency cap or guarantee against pressure; no mlock is used.
        return touch + 512
    return memory


def host_snapshot(shell):
    """Read-only observations; missing optional statistics never change policy."""
    paths = {
        "boot_id": "/proc/sys/kernel/random/boot_id",
        "memory": "/proc/meminfo",
        "buddy": "/proc/buddyinfo",
        "shmem_thp": "/sys/kernel/mm/transparent_hugepage/shmem_enabled",
        "anonymous_thp": "/sys/kernel/mm/transparent_hugepage/enabled",
        "defrag": "/sys/kernel/mm/transparent_hugepage/defrag",
        **{
            f"shmem_{size}k": f"/sys/kernel/mm/transparent_hugepage/hugepages-{size}kB/shmem_enabled"
            for size in (16, 32, 64)
        },
    }
    result = {}
    for label, path in paths.items():
        try:
            result[label] = shell("cat " + shlex.quote(path))
        except (subprocess.SubprocessError, OSError):
            result[label] = "unavailable"
    result["selinux"] = shell("getenforce")
    return result


def verified_boot_guard(vmm_text):
    """Accept old and extended observations, rejecting any failed margin advice."""
    lines = [line for line in vmm_text.splitlines() if line.startswith("PLB_BACKING_BOOT_GUARD")]
    if not lines or "PLB_BACKING_FOLIOS" not in vmm_text:
        return False
    pattern = re.compile(
        r"PLB_BACKING_BOOT_GUARD prefix_mib=64(?: tail_mib=(0|64))? "
        r"low_result=0 high_result=0(?: tail_result=(0))?"
    )
    for line in lines:
        match = pattern.fullmatch(line)
        if not match or (match[1] is not None and match[2] is None):
            return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="Explicit Galaxy adb serial")
    parser.add_argument("--adb", default="adb", help="ADB executable path")
    parser.add_argument("--server-port", type=int, default=5037, help="Local ADB server port")
    parser.add_argument("--su", action="store_true", help="Use already-permitted root escalation")
    parser.add_argument(
        "--initial-mount-namespace",
        action="store_true",
        help="Enter PID 1's mount namespace for this command only; no mounts are changed",
    )
    parser.add_argument("--dump-fdt", action="store_true", help="Capture the generated guest DTB")
    parser.add_argument("--kernel", choices=KERNELS, required=True)
    parser.add_argument(
        "--memory", type=int, choices=(512, 768, 1024, 2048, 4096, 5120), required=True
    )
    parser.add_argument("--touch", type=int, required=True, help="Guest MiB to allocate and touch")
    parser.add_argument("--rounds", type=int, choices=range(1, 9), default=1)
    parser.add_argument(
        "--discard-between-rounds",
        action="store_true",
        help="Guest-only MADV_DONTNEED, zero-fill check and physical-page reuse observation",
    )
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
    backing.add_argument(
        "--shared-prefault-order",
        choices=("forward", "reverse"),
        help="Diskless research: retain original shared memfd and prefault in the selected order",
    )
    backing.add_argument(
        "--boot-safe-hugepages",
        action="store_true",
        help="Diskless research: retain base pages for the first 64 MiB of main RAM",
    )
    parser.add_argument(
        "--refault-small-pages",
        action="store_true",
        help="Before donation only: preserve bytes while refaulting owned small shmem pages",
    )
    parser.add_argument(
        "--lock-backing",
        action="store_true",
        help="Before donation only: process-local mlock2 on-fault for the owned guest mapping",
    )
    parser.add_argument(
        "--collapse-backing",
        action="store_true",
        help="Before donation only: bounded 2 MiB collapse requests on owned backing",
    )
    parser.add_argument(
        "--approved-small-shmem-policy",
        action="store_true",
        help="Requires explicit human approval: temporary three-key host shmem policy trial",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        budget = workload_budget_mib(
            args.memory,
            args.touch,
            args.rounds,
            args.anonymous_backing
            or args.prefault_backing
            or args.shared_prefault_order
            or args.boot_safe_hugepages,
            args.hugepages,
            args.discard_between_rounds,
            args.boot_safe_hugepages
            and args.refault_small_pages
            and args.approved_small_shmem_policy,
        )
    except ValueError as error:
        parser.error(str(error))
    if args.boot_safe_hugepages and not args.approved_small_shmem_policy:
        parser.error("Boot-region comparison requires the explicitly approved bounded policy stage")
    if args.refault_small_pages and not args.boot_safe_hugepages:
        parser.error("Refault comparison requires --boot-safe-hugepages")
    if args.lock_backing and (not args.refault_small_pages or args.collapse_backing):
        parser.error("On-fault locking requires refault comparison without collapse")
    if args.lock_backing and (args.memory, args.touch) not in ((512, 256), (5120, 4096)):
        parser.error("On-fault locking has only the prepared 512/256 and 5120/4096 profiles")
    if args.collapse_backing and not args.refault_small_pages:
        parser.error("Collapse comparison requires --refault-small-pages")
    if args.approved_small_shmem_policy and (
        args.kernel != "linux618"
        or (args.memory, args.touch)
        not in ((512, 256), (1024, 768), (2048, 1536), (4096, 3072), (5120, 4096))
        or (args.memory >= 2048 and not (args.boot_safe_hugepages and args.refault_small_pages))
        or args.rounds != 1
        or not args.hugepages
        or args.dump_fdt
        or args.discard_between_rounds
        or args.anonymous_backing
        or args.prefault_backing
        or args.shared_prefault_order
    ):
        parser.error(
            "Approved policy VM stages: linux618 512/256 or 1024/768, one round, --hugepages"
        )
    if not 1024 <= args.server_port <= 65535:
        parser.error("Invalid local ADB server port")
    adb = [args.adb, "-P", str(args.server_port), "-s", args.serial]

    def remote_command(command):
        if args.initial_mount_namespace:
            command = "nsenter -t 1 -m -- /system/bin/sh -c " + shlex.quote(command)
        if args.su:
            command = "su -c " + shlex.quote(command)
        return command

    def shell(command):
        return subprocess.check_output(
            adb + ["shell", remote_command(command)], text=True, timeout=15
        ).strip()

    if shell("getprop ro.product.model") != "SM-S948Q" or shell("id -u") != "0":
        parser.error("Expected the explicitly selected Galaxy SM-S948Q with root adbd")
    info = shell("cat /proc/meminfo")
    available = int(
        next(line.split()[1] for line in info.splitlines() if line.startswith("MemAvailable:"))
    )
    if available < (budget + 768) * 1024:
        parser.error("Insufficient host memory headroom; host policies will not be changed")
    if args.dump_fdt and (
        args.anonymous_backing or args.prefault_backing or args.shared_prefault_order
    ):
        parser.error("DTB capture uses only the original VMM and original shared-memory backing")
    if (
        args.anonymous_backing
        or args.prefault_backing
        or args.shared_prefault_order
        or args.boot_safe_hugepages
    ):
        hashes = shell(
            "sha256sum /apex/com.android.virt/bin/crosvm " + FILES + "/kernel-probes/crosvm-copy"
        ).splitlines()
        if len(hashes) != 2 or hashes[0].split()[0] != hashes[1].split()[0]:
            parser.error("The private diagnostic crosvm copy must match the installed VMM exactly")
    args.output.mkdir(parents=True, exist_ok=True)
    label = f"{args.kernel}-{args.memory}-{args.touch}" + ("-huge" if args.hugepages else "")
    label += f"-rounds{args.rounds}"
    if args.approved_small_shmem_policy:
        label += "-approved-small-shmem"
    if args.discard_between_rounds:
        label += "-discard"
    if args.boot_safe_hugepages:
        label += "-boot-safe"
    if args.refault_small_pages:
        label += "-refault"
    if args.lock_backing:
        label += "-locked"
    if args.collapse_backing:
        label += "-collapse"
    if args.anonymous_backing or args.prefault_backing or args.shared_prefault_order:
        label += (
            ("-shared-" + args.shared_prefault_order)
            if args.shared_prefault_order
            else ("-prefault" if args.prefault_backing else "-anonymous")
        )
    output = args.output / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ-") + label)
    output.mkdir(mode=0o700)
    before = host_snapshot(shell)
    if before["selinux"] != "Enforcing":
        parser.error("Expected unchanged host SELinux Enforcing; refusing the probe")
    (output / "host-before.json").write_text(json.dumps(before, indent=2) + "\n")

    def capture_kernel(label):
        # Raw diagnostics may contain addresses/identifiers. Keep these only in
        # the ignored private output directory. Never clear the kernel buffer.
        try:
            text = shell("dmesg | tail -c 2097152")
        except (subprocess.SubprocessError, OSError):
            text = "unavailable"
        (output / label).write_text(text + "\n")

    capture_kernel("kernel-before.private.log")
    if args.lock_backing:
        backing_mode = 8
    elif args.collapse_backing:
        backing_mode = 7
    elif args.refault_small_pages:
        backing_mode = 6
    elif args.boot_safe_hugepages:
        backing_mode = 5
    elif args.shared_prefault_order == "reverse":
        backing_mode = 4
    elif args.shared_prefault_order == "forward":
        backing_mode = 3
    elif args.prefault_backing:
        backing_mode = 2
    elif args.anonymous_backing:
        backing_mode = 1
    else:
        backing_mode = 0
    command = [
        FILES + "/kernel-probes/memory-runner",
        args.kernel,
        str(args.memory),
        str(args.touch),
        "1" if args.hugepages else "0",
        str(backing_mode),
    ]
    command.extend(
        [
            "1" if args.dump_fdt else "0",
            str(args.rounds),
            "1" if args.discard_between_rounds else "0",
        ]
    )
    if args.approved_small_shmem_policy:
        command = [
            FILES + "/kernel-probes/small-shmem-policy-probe",
            "--apply-approved-small-shmem",
            f"--vm-{args.memory}-{args.touch}"
            + ("-boot-safe" if args.boot_safe_hugepages else "")
            + ("-refault" if args.refault_small_pages else "")
            + ("-collapse" if args.collapse_backing else "")
            + ("-locked" if args.lock_backing else ""),
        ]
    with (output / "vmm.log").open("w") as out:
        result = subprocess.run(
            adb + ["shell", remote_command(shlex.join(command))],
            stdout=out,
            stderr=subprocess.STDOUT,
            timeout=50 if args.approved_small_shmem_policy else 35,
        )
    capture_kernel("kernel-after.private.log")
    log = FILES + "/kernel-probes/memory-live.log"
    guest_log = output / "guest.log"
    with guest_log.open("wb") as stream:
        pull = subprocess.run(
            adb + ["shell", "-T", remote_command("head -c 2097152 " + shlex.quote(log))],
            stdout=stream,
            stderr=subprocess.PIPE,
            timeout=15,
        )
    data = guest_log.read_text(errors="replace") if pull.returncode == 0 else ""
    summary = {
        "kernel": args.kernel,
        "memory": args.memory,
        "touch": args.touch,
        "rounds": args.rounds,
        "discard_between_rounds": args.discard_between_rounds,
        "hugepages": args.hugepages,
        "anonymous_backing": args.anonymous_backing,
        "prefault_backing": args.prefault_backing,
        "shared_prefault_order": args.shared_prefault_order,
        "boot_safe_hugepages": args.boot_safe_hugepages,
        "refault_small_pages": args.refault_small_pages,
        "collapse_backing": args.collapse_backing,
        "lock_backing": args.lock_backing,
        "sparse_collapse_seeded": "PLB_BACKING_SPARSE_SEED" in (output / "vmm.log").read_text(),
        "dump_fdt": args.dump_fdt,
        "approved_small_shmem_policy": args.approved_small_shmem_policy,
        "exit": result.returncode,
        **assess_probe(
            result.returncode,
            data,
            (output / "vmm.log").read_text(),
            args.touch,
            args.rounds,
            args.discard_between_rounds,
        ),
    }
    started = (
        f"PLB_RUNNER_START memory_mib={args.memory} touch_mib={args.touch} "
        f"rounds={args.rounds} discard={int(args.discard_between_rounds)}"
        in (output / "vmm.log").read_text().splitlines()
    )
    if not started:
        summary.update(complete=False, run_not_started=True, stages=[])
    vmm_text = (output / "vmm.log").read_text()
    backing_marker = (
        "PLB_BACKING_SHARED"
        if args.shared_prefault_order or args.boot_safe_hugepages
        else "PLB_BACKING_REPLACED"
    )
    if (
        args.anonymous_backing
        or args.prefault_backing
        or args.shared_prefault_order
        or args.boot_safe_hugepages
    ) and backing_marker not in vmm_text:
        summary.update(complete=False, backing_not_applied=True)
    if (
        args.prefault_backing or args.shared_prefault_order or args.boot_safe_hugepages
    ) and "PLB_BACKING_PREFAULTED" not in vmm_text:
        summary.update(complete=False, prefault_not_applied=True)
    if args.boot_safe_hugepages and not verified_boot_guard(vmm_text):
        summary.update(complete=False, boot_guard_not_verified=True)
    if args.refault_small_pages and not re.search(
        r"PLB_SHMEM_REFAULT round=4 block_bytes=32768 kept_large_windows=\d+ retried=\d+ errors=0",
        vmm_text,
    ):
        summary.update(complete=False, refault_not_verified=True)
    if args.lock_backing and "PLB_BACKING_LOCK_ONFAULT result=0 errno=0" not in vmm_text:
        summary.update(complete=False, backing_lock_not_verified=True)
    if args.collapse_backing and "PLB_BACKING_COLLAPSE attempted=" not in vmm_text:
        summary.update(complete=False, collapse_not_attempted=True)
    if args.dump_fdt:
        dtb = output / "guest-generated.dtb"
        with dtb.open("wb") as stream:
            capture = subprocess.run(
                adb
                + [
                    "shell",
                    "-T",
                    remote_command("head -c 1048576 " + FILES + "/kernel-probes/memory-last.dtb"),
                ],
                stdout=stream,
                stderr=subprocess.PIPE,
                timeout=15,
            )
        summary["fdt_dump_saved"] = capture.returncode == 0 and valid_fdt_dump(dtb.read_bytes())
    after = host_snapshot(shell)
    (output / "host-after.json").write_text(json.dumps(after, indent=2) + "\n")
    summary["host_boundary_unchanged"] = (
        before["boot_id"] != "unavailable"
        and before["boot_id"] == after["boot_id"]
        and after["selinux"] == "Enforcing"
    )
    if args.approved_small_shmem_policy:
        policy_keys = (
            "shmem_16k",
            "shmem_32k",
            "shmem_64k",
            "shmem_thp",
            "anonymous_thp",
            "defrag",
        )
        summary["host_policy_restored"] = (
            "PLB_POLICY_RESTORED=1 HOST_BOUNDARY_UNCHANGED=1" in vmm_text
            and "PLB_POLICY_GUARDIAN_RESTORED=1" in vmm_text
            and all(
                before[key] != "unavailable" and before[key] == after[key] for key in policy_keys
            )
        )
        if not summary["host_policy_restored"]:
            summary["complete"] = False
    (output / "result.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))
    raise SystemExit(
        0
        if summary["complete"]
        and summary["host_boundary_unchanged"]
        and (not args.dump_fdt or summary["fdt_dump_saved"])
        else 1
    )


if __name__ == "__main__":
    main()
