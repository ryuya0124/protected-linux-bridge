#!/usr/bin/env python3
"""Build isolated 1.5/2 GiB Android guest diagnostics; never install or launch them.

The policy variant requires separate explicit authorization for the three
host-wide small-shmem settings. Nothing here is included in a released APK.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    args = parser.parse_args()
    if shutil.disk_usage(ROOT).free < 21 * 1024**3:
        parser.error("Retain at least 20 GiB Mac storage headroom")
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    out = ROOT / "artifacts/android-display/memory-research"
    out.mkdir(mode=0o700, parents=True, exist_ok=True)
    setup = (ROOT / "native/android_guest_setup.sh").read_text()
    (out / "android_guest_setup.generated.h").write_text(
        "/* Generated guest-only bootstrap command. */\n"
        + "static const char android_setup_service[] =\n"
        + json.dumps("exec:sh -c " + shlex.quote(setup))
        + ";\n"
    )
    flags = [str(compiler), "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(out)]
    library = out / "libandroid_memory_backing_probe.so"
    subprocess.run(
        flags
        + [
            "-DPLB_ANDROID_SHARED_BACKING_PROBE=1",
            "-shared",
            "-fPIC",
            str(ROOT / "tools/memory_backing_probe.c"),
            "-o",
            str(library),
        ],
        check=True,
    )
    digest = hashlib.sha256(library.read_bytes()).hexdigest()
    products = [library]
    for memory, policy in ((2048, False), (2048, True), (1536, False), (1536, True)):
        stem = "android-memory" if memory == 2048 else "android-memory1536"
        binary = out / (stem + ("-policy-runner" if policy else "-runner"))
        defines = ["-DPLB_ANDROID_MEMORY_PROBE=1", f"-DPLB_ANDROID_MEMORY_MIB={memory}"]
        if policy:
            defines += [
                "-DPLB_ANDROID_MEMORY_POLICY=1",
                f'-DPLB_ANDROID_MEMORY_LIBRARY_SHA256="{digest}"',
            ]
        subprocess.run(
            flags + defines + [str(ROOT / "tools/android_probe_runner.c"), "-o", str(binary)],
            check=True,
        )
        products.append(binary)
    controller = out / "small-shmem-policy-probe"
    subprocess.run(
        flags + [str(ROOT / "tools/small_shmem_policy_probe.c"), "-o", str(controller)],
        check=True,
    )
    products.append(controller)
    sources = [
        ROOT / name
        for name in (
            "tools/build_android_memory_probe.py",
            "tools/android_memory_observer.h",
            "tools/android_probe_runner.c",
            "tools/small_shmem_policy_probe.c",
            "tools/memory_backing_probe.c",
            "tools/shmem_refault_probe.h",
            "tools/owned_page_summary.h",
            "tools/backing_order_summary.h",
            "tools/diagnostic_owner_lock.h",
            "native/android_guest_manager.c",
            "native/android_guest_adb.h",
            "native/android_guest_setup.sh",
            "native/android_sensor_transport.h",
            "native/profile_lock.h",
        )
    ]
    manifest = {
        "purpose": "development-only isolated Android 17 guest memory diagnostic",
        "memory_mib": [1536, 2048],
        "host_policy_seconds": 10,
        "independent_policy_watchdog_seconds": 40,
        "android_observation_seconds": 120,
        "sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sources + products
        },
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(out)


if __name__ == "__main__":
    main()
