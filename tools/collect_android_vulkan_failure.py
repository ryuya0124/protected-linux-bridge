#!/usr/bin/env python3
"""Read-only Galaxy crash collection; never start a VM, reboot or clear logs.

Every command's stdout/stderr is saved even on failure. Raw output stays in an
ignored private artifact folder. A missing root shell is recorded, not repaired.
"""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FILES = "/data/user/0/net.ryuya_dev.protectedlinux/files"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", type=Path, required=True)
    parser.add_argument(
        "--serial", required=True, help="Explicitly selected Galaxy; never auto-select"
    )
    parser.add_argument("--adb-port", default="5038")
    args = parser.parse_args()
    adb = [str(args.adb), "-P", args.adb_port, "-s", args.serial]
    identity = subprocess.run(
        adb + ["shell", "getprop ro.product.model"], capture_output=True, timeout=8
    )
    if identity.returncode or identity.stdout.strip() != b"SM-S948Q":
        raise ValueError("Selected Galaxy identity unavailable; no collection attempted")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = ROOT / "artifacts/android-vulkan" / ("host-restart-" + stamp)
    output.mkdir(parents=True, exist_ok=False, mode=0o700)
    records = {}

    def capture(name, command, root=False, timeout=12):
        remote = "su -c " + shlex.quote(command) if root else command
        try:
            result = subprocess.run(
                adb + ["shell", "-T", remote], capture_output=True, timeout=timeout
            )
            stdout, stderr, code = result.stdout, result.stderr, result.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr, code = error.stdout or b"", error.stderr or b"", None
        (output / (name + ".stdout.private.txt")).write_bytes(stdout)
        (output / (name + ".stderr.private.txt")).write_bytes(stderr)
        records[name] = {
            "exit_code": code,
            "stdout_bytes": len(stdout),
            "stderr_bytes": len(stderr),
        }
        return code, stdout

    capture(
        "boot",
        "id; cat /proc/sys/kernel/random/boot_id; uptime; "
        "getprop sys.boot.reason; getprop sys.boot.reason.last; "
        "getprop ro.boot.bootreason; getprop ro.build.fingerprint; getenforce",
    )
    capture("boot-log", "logcat -d -b crash -t 1000")
    code, stdout = capture("root-check", "id", root=True, timeout=10)
    if code == 0 and b"uid=0(root)" in stdout:
        capture("kernel", "cat /proc/sys/kernel/tainted; dmesg | tail -n 2000", root=True)
        capture("pstore-list", "ls -ln /sys/fs/pstore", root=True)
        for name in (
            "console-ramoops",
            "console-ramoops-0",
            "dmesg-ramoops-0",
            "dmesg-ramoops-1",
            "pmsg-ramoops-0",
        ):
            capture(name, "head -c 2097152 /sys/fs/pstore/" + name, root=True)
        capture("last-kmsg", "head -c 2097152 /proc/last_kmsg", root=True)
        capture(
            "policies",
            "for n in 16 32 64 128 256 512; do "
            "cat /sys/kernel/mm/transparent_hugepage/hugepages-${n}kB/shmem_enabled; done",
            root=True,
        )
        for name in ("android-vmm.log", "android-console.log"):
            capture(
                name,
                "tail -c 2097152 " + FILES + "/android-guests/cf17-vulkan4096/" + name,
                root=True,
            )
    (output / "collection.private.json").write_text(json.dumps(records, indent=2) + "\n")
    print("Read-only collection saved:", output)
    print("Root logs collected:", code == 0 and b"uid=0(root)" in stdout)


if __name__ == "__main__":
    main()
