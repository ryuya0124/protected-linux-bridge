#!/usr/bin/env python3
"""Build the execution-disabled Vulkan investigation. No device I/O or VM start.

The first trial reached guest ADB; the second ended with a retained host panic.
Manager, policy and RAM proxy now refuse at entry, before allocation or writes.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
VMM_SHA = "13dd50f7e347c597c16c49151c677a0798b5b9c8c8971b9643165ac3165ac0ca"
KERNEL_SHA = "4ae0f63515fe8a16945007b26423c0a6eccb8b04119259bd424d5e15894b52f5"
RAMDISK_SHA = "4ef88a3ce9fdda207e0bfabe1d28ae24319848dcedf613d60ba61e0936f620d5"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--renderer-sha256", required=True)
    parser.add_argument("--profile-hashes", type=Path, required=True)
    args = parser.parse_args()
    if (
        not re.fullmatch("[0-9a-f]{64}", args.renderer_sha256)
        or digest(args.renderer) != args.renderer_sha256
    ):
        raise ValueError("Explicitly reviewed private renderer identity required")
    source = ROOT / "artifacts/android4g-gpu"
    vmm = source / "vmm-build/target/aarch64-linux-android/release/crosvm"
    if digest(vmm) != VMM_SHA:
        raise ValueError("Retain the measured baseline VMM")
    hashes = json.loads(args.profile_hashes.read_text())
    names = ("android-work.qcow2", "android-misc.raw", "android-frp.raw", "android-lowram.cpio")
    if set(hashes) != set(names) or any(
        not re.fullmatch("[0-9a-f]{64}", x) for x in hashes.values()
    ):
        raise ValueError("Four read-only source profile hashes required")
    if hashes["android-lowram.cpio"] != RAMDISK_SHA:
        raise ValueError("Retain the measured guest initramfs")
    output = ROOT / "artifacts/android-vulkan/runtime"
    output.mkdir(parents=True, exist_ok=True)
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    base = [str(compiler), "-O2", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(output)]
    modes = [
        "-DPLB_ANDROID4G_INTERACTIVE=1",
        "-DPLB_ANDROID4G_VULKAN_DIAGNOSTIC=1",
        "-DPLB_ANDROID4G_CUSTOM_GUEST=1",
        "-DPLB_ANDROID4G_BUFFER_CAPSET=1",
        "-DPLB_ANDROID4G_NORMAL_FIRST=1",
        "-DPLB_ANDROID4G_LARGER_DMA_POOL=1",
    ]

    def build(name, source, flags=()):
        subprocess.run(
            base + modes + list(flags) + [str(ROOT / source), "-o", str(output / name)], check=True
        )

    shutil.copyfile(vmm, output / "crosvm")
    shutil.copyfile(args.renderer, output / "libgfxstream_backend.so")
    (output / "preallocated_vm.generated.h").write_text(
        f'#define PLB_PREALLOCATED_VMM_SHA256 "{VMM_SHA}"\n'
        f'#define PLB_PREALLOCATED_KERNEL_SHA256 "{KERNEL_SHA}"\n'
        f'#define PLB_PREALLOCATED_INITRAMFS_SHA256 "{RAMDISK_SHA}"\n'
        f'#define PLB_PREALLOCATED_RENDERER_SHA256 "{args.renderer_sha256}"\n'
    )
    build("adopt-vmm", "tools/preallocated_vm_probe.c", ["-DPLB_PREALLOCATED_ANDROID_PROXY=1"])
    (output / "android4g.generated.h").write_text(
        f'#define PLB_ANDROID4G_VMM_SHA256 "{VMM_SHA}"\n'
        f'#define PLB_ANDROID4G_PROXY_SHA256 "{digest(output / "adopt-vmm")}"\n'
        f'#define PLB_ANDROID4G_KERNEL_SHA256 "{KERNEL_SHA}"\n'
        f'#define PLB_ANDROID4G_INITRAMFS_SHA256 "{RAMDISK_SHA}"\n'
        f'#define PLB_ANDROID4G_RENDERER_SHA256 "{args.renderer_sha256}"\n'
    )
    for name in ("android_guest_setup.generated.h", "android_gpu_early.generated.h"):
        shutil.copyfile(ROOT / "app/build/native/android-gpu4g" / name, output / name)
    build(
        "manager",
        "tools/android_probe_runner.c",
        ["-DPLB_ANDROID_ADOPTED_PROBE=1", "-DPLB_ANDROID_GFXSTREAM_PROBE=1"],
    )
    build(
        "policy",
        "tools/small_shmem_policy_probe.c",
        ["-DPLB_PREALLOCATED_VM_POLICY=1", "-DPLB_MEDIUM_SHMEM_PROPOSAL=1"],
    )
    for name in ("allocation-probe", "pressure-check"):
        subprocess.run(
            base + [str(ROOT / "tools/backing_allocation_probe.c"), "-o", str(output / name)],
            check=True,
        )
    runtime = {
        name: digest(output / name)
        for name in (
            "crosvm",
            "adopt-vmm",
            "manager",
            "policy",
            "allocation-probe",
            "pressure-check",
            "libgfxstream_backend.so",
        )
    }
    (output / "android_surface_profile.generated.h").write_text(
        "static const struct copy_item profile_items[] = {\n"
        + "".join('{"' + n + '", "' + hashes[n] + '"},\n' for n in names)
        + "};\n"
        + "static const struct copy_item runtime_items[] = {\n"
        + "".join('{"' + n + '", "' + value + '"},\n' for n, value in runtime.items())
        + "};\n"
    )
    subprocess.run(
        base
        + [
            "-DPLB_ANDROID4G_VULKAN_DIAGNOSTIC=1",
            str(ROOT / "tools/android_surface_profile.c"),
            "-o",
            str(output / "profile-copy"),
        ],
        check=True,
    )
    (output / "build.json").write_text(
        json.dumps(
            {
                "build_starts_vm": False,
                "execution_disabled_after_host_restart": True,
                "developer_only": True,
                "session_seconds": 240,
                "configured_memory_mib": 4096,
                "guest_kernel_sha256": KERNEL_SHA,
                "guest_initramfs_sha256": RAMDISK_SHA,
                "runtime": runtime,
                "profile_copy_sha256": digest(output / "profile-copy"),
                "original_disks_written": False,
                "uses_ld_preload": False,
            },
            indent=2,
        )
        + "\n"
    )
    print("Built Vulkan investigation with VM execution disabled; no device operation")


if __name__ == "__main__":
    main()
