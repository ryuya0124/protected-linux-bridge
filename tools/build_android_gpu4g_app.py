#!/usr/bin/env python3
"""Bundle the exact, explicitly selected Android/GLES app profile; no device I/O.

The kernel and private VMM inputs must match the measured separate builds.
Absent inputs omit this optional bundle instead of weakening pins or changing
legacy profiles. Preparing the bundle never starts a VM or changes host policy.
"""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from build_android4g_probe import custom_ramdisk
from build_android_gpu_probe import early_command, gles_ramdisk

KERNEL_SHA = "4ae0f63515fe8a16945007b26423c0a6eccb8b04119259bd424d5e15894b52f5"
VMM_SHA = "13dd50f7e347c597c16c49151c677a0798b5b9c8c8971b9643165ac3165ac0ca"
RAMDISK_SHA = "4ef88a3ce9fdda207e0bfabe1d28ae24319848dcedf613d60ba61e0936f620d5"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(root, sdk, output, compiler):
    source = root / "artifacts/android4g-gpu"
    kernel = source / "guest-kernel-system-heap/Image-android17-dma-sync"
    vmm = source / "vmm-build/target/aarch64-linux-android/release/crosvm"
    original = root / "artifacts/android-display/cuttlefish17/android-lowram.cpio"
    manifest = source / "guest-kernel-system-heap/build.json"
    if not all(p.is_file() for p in (kernel, vmm, original, manifest)):
        # Remove only this bundle's generated named outputs, so a stale prior
        # build cannot accidentally re-enable it after inputs are removed.
        for name in (
            "crosvm",
            "guest.Image",
            "ramdisk-tail.cpio",
            "adopt-vmm",
            "manager",
            "policy",
            "allocation-probe",
            "pressure-check",
            "network-controller",
        ):
            (output / ("gpu4g-" + name)).unlink(missing_ok=True)
        (output / "android-gpu4g-bridge").unlink(missing_ok=True)
        print("Android GPU4G: pinned optional inputs absent; existing profiles only")
        return
    record = json.loads(manifest.read_text())
    if (
        digest(kernel) != KERNEL_SHA
        or digest(vmm) != VMM_SHA
        or record.get("kernel_sha256") != KERNEL_SHA
        or record.get("kernel_release") != "6.12.74-plb-android-gpu2"
        or record.get("system_heap") is not True
    ):
        raise ValueError("Android GPU4G measured input identity mismatch")
    temporary = root / "app/build/native/android-gpu4g"
    temporary.mkdir(parents=True, exist_ok=True)
    original_bytes = original.read_bytes()
    complete = custom_ramdisk(gles_ramdisk(original_bytes), record)
    if hashlib.sha256(complete).hexdigest() != RAMDISK_SHA or not complete.startswith(
        original_bytes
    ):
        raise ValueError("Android GPU4G ramdisk identity mismatch")
    # Ship only the small guest-property/module-list overlay, never the stock
    # Android OS initramfs. Root appends it to the verified retained original.
    (output / "gpu4g-ramdisk-tail.cpio").write_bytes(complete[len(original_bytes) :])
    (output / "gpu4g-android-lowram.cpio").unlink(missing_ok=True)
    shutil.copyfile(kernel, output / "gpu4g-guest.Image")
    shutil.copyfile(vmm, output / "gpu4g-crosvm")
    flags = [str(compiler), "-O2", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(temporary)]
    # Guest networking is compiled from the pinned HEV sources and the actual
    # guest framework signatures. It is requested only after guest readiness.
    import sys

    for script in ("build_android_network_agent.py", "build_android_network_trial.py"):
        subprocess.run(
            [sys.executable, str(root / "tools" / script), "--sdk", str(sdk)], check=True
        )
    network = root / "artifacts/android4g-gpu/network-agent"
    subprocess.run(
        [
            str(compiler),
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(network),
            "-DPLB_CID=39821",
            "-DPLB_PROXY_PORT=50442",
            '-DPLB_PROXY_LOG="/data/user/0/net.ryuya_dev.protectedlinux/files/android-network-proxy.log"',
            str(root / "native/android_network_controller.c"),
            str(root / "native/network_proxy.c"),
            "-o",
            str(output / "gpu4g-network-controller"),
        ],
        check=True,
    )

    modes = [
        "-DPLB_ANDROID4G_INTERACTIVE=1",
        "-DPLB_ANDROID4G_CUSTOM_GUEST=1",
        "-DPLB_ANDROID4G_BUFFER_CAPSET=1",
        "-DPLB_ANDROID4G_DISABLE_RENDERER_VULKAN=1",
        "-DPLB_ANDROID4G_NORMAL_FIRST=1",
        "-DPLB_ANDROID4G_LARGER_DMA_POOL=1",
    ]
    (temporary / "preallocated_vm.generated.h").write_text(
        f'#define PLB_PREALLOCATED_VMM_SHA256 "{VMM_SHA}"\n'
        f'#define PLB_PREALLOCATED_KERNEL_SHA256 "{KERNEL_SHA}"\n'
        f'#define PLB_PREALLOCATED_INITRAMFS_SHA256 "{RAMDISK_SHA}"\n'
    )
    subprocess.run(
        flags
        + modes
        + [
            "-DPLB_PREALLOCATED_ANDROID_PROXY=1",
            str(root / "tools/preallocated_vm_probe.c"),
            "-o",
            str(output / "gpu4g-adopt-vmm"),
        ],
        check=True,
    )
    (temporary / "android4g.generated.h").write_text(
        f'#define PLB_ANDROID4G_VMM_SHA256 "{VMM_SHA}"\n'
        f'#define PLB_ANDROID4G_PROXY_SHA256 "{digest(output / "gpu4g-adopt-vmm")}"\n'
        f'#define PLB_ANDROID4G_KERNEL_SHA256 "{KERNEL_SHA}"\n'
        f'#define PLB_ANDROID4G_INITRAMFS_SHA256 "{RAMDISK_SHA}"\n'
    )
    import shlex

    setup = (root / "native/android_guest_setup.sh").read_text()
    (temporary / "android_guest_setup.generated.h").write_text(
        "static const char android_setup_service[] =\n"
        + json.dumps("exec:sh -c " + shlex.quote(setup))
        + ";\n"
    )
    (temporary / "android_gpu_early.generated.h").write_text(
        "static const char console_swap_command[] =\n"
        + json.dumps(early_command((root / "tools/android_gpu_early_setup.sh").read_text()))
        + ";\n"
    )
    subprocess.run(
        flags
        + modes
        + [
            "-DPLB_ANDROID_ADOPTED_PROBE=1",
            "-DPLB_ANDROID_GFXSTREAM_PROBE=1",
            str(root / "tools/android_probe_runner.c"),
            "-o",
            str(output / "gpu4g-manager"),
        ],
        check=True,
    )
    subprocess.run(
        flags
        + modes
        + [
            "-DPLB_PREALLOCATED_VM_POLICY=1",
            "-DPLB_MEDIUM_SHMEM_PROPOSAL=1",
            str(root / "tools/small_shmem_policy_probe.c"),
            "-o",
            str(output / "gpu4g-policy"),
        ],
        check=True,
    )
    subprocess.run(
        flags
        + [
            str(root / "tools/backing_allocation_probe.c"),
            "-o",
            str(output / "gpu4g-allocation-probe"),
        ],
        check=True,
    )
    shutil.copyfile(output / "gpu4g-allocation-probe", output / "gpu4g-pressure-check")
    names = (
        "crosvm",
        "guest.Image",
        "ramdisk-tail.cpio",
        "adopt-vmm",
        "manager",
        "policy",
        "allocation-probe",
        "pressure-check",
        "network-controller",
    )
    (temporary / "gpu4g_install.generated.h").write_text(
        "/* Exact APK payload identities, copied and verified again by root. */\n"
        "static const struct gpu4g_artifact gpu4g_artifacts[] = {\n"
        + "".join(
            '{"gpu4g-'
            + name
            + '", "'
            + name
            + '", "'
            + digest(output / ("gpu4g-" + name))
            + '"},\n'
            for name in names
        )
        + "};\n"
    )
    subprocess.run(
        flags
        + [
            str(root / "native/android_gpu4g_launcher.c"),
            "-o",
            str(output / "android-gpu4g-bridge"),
        ],
        check=True,
    )
    print("Android GPU4G: built explicit 4096 MiB GLES profile, 128 MiB DMA pool; no VM started")
