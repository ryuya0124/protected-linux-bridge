#!/usr/bin/env python3
"""Build the fixed developer-only Surface runtime; no device I/O or VM launch."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
KERNEL = "4ae0f63515fe8a16945007b26423c0a6eccb8b04119259bd424d5e15894b52f5"
RAMDISK = "4ef88a3ce9fdda207e0bfabe1d28ae24319848dcedf613d60ba61e0936f620d5"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument(
        "--profile-hashes",
        type=Path,
        help="Read-only hashes of the verified offline source profile",
    )
    parser.add_argument(
        "--product-assets",
        type=Path,
        help="Build the explicitly selectable APK variant into this asset directory",
    )
    args = parser.parse_args()
    product = args.product_assets is not None
    output = ROOT / (
        "app/build/native/android-surface" if product else "artifacts/android4g-gpu/surface-runtime"
    )
    output.mkdir(parents=True, exist_ok=True)
    library = (
        ROOT
        / "artifacts/android4g-gpu/vmm-build/target/aarch64-linux-android/release/libplb_crosvm_surface.so"
    )
    probe = ROOT / "artifacts/android4g-gpu/surface-probe"
    if product:
        from build_android_surface_agent import build as build_agent

        code, jni = build_agent(args.sdk, output, library)
    else:
        code = probe / "build/outputs/apk/release/PlbSurfaceProbe-release.apk"
        jni = probe / "libsurface-probe.so"
    inputs = {"LIBRARY": library, "JNI": jni, "APK": code}
    (output / "android_surface.generated.h").write_text(
        "".join(
            "#define PLB_SURFACE_" + name + '_SHA256 "' + digest(path) + '"\n'
            for name, path in inputs.items()
        )
    )
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
        "-DPLB_ANDROID4G_SURFACE_INTERACTIVE=1",
        "-DPLB_ANDROID4G_CUSTOM_GUEST=1",
        "-DPLB_ANDROID4G_BUFFER_CAPSET=1",
        "-DPLB_ANDROID4G_DISABLE_RENDERER_VULKAN=1",
        "-DPLB_ANDROID4G_NORMAL_FIRST=1",
        "-DPLB_ANDROID4G_LARGER_DMA_POOL=1",
    ]

    if product:
        modes.append("-DPLB_SURFACE_PRODUCT=1")

    def build(name, source, flags=()):
        subprocess.run(
            base + modes + list(flags) + [str(ROOT / source), "-o", str(output / name)], check=True
        )

    build("crosvm", "tools/android_surface_vmm.c")
    vmm = digest(output / "crosvm")
    (output / "preallocated_vm.generated.h").write_text(
        f'#define PLB_PREALLOCATED_VMM_SHA256 "{vmm}"\n'
        f'#define PLB_PREALLOCATED_KERNEL_SHA256 "{KERNEL}"\n'
        f'#define PLB_PREALLOCATED_INITRAMFS_SHA256 "{RAMDISK}"\n'
    )
    build("adopt-vmm", "tools/preallocated_vm_probe.c", ["-DPLB_PREALLOCATED_ANDROID_PROXY=1"])
    (output / "android4g.generated.h").write_text(
        f'#define PLB_ANDROID4G_VMM_SHA256 "{vmm}"\n'
        f'#define PLB_ANDROID4G_PROXY_SHA256 "{digest(output / "adopt-vmm")}"\n'
        f'#define PLB_ANDROID4G_KERNEL_SHA256 "{KERNEL}"\n'
        f'#define PLB_ANDROID4G_INITRAMFS_SHA256 "{RAMDISK}"\n'
    )
    # The same fixed setup/console headers as the measured app profile.
    generated = ROOT / "app/build/native/android-gpu4g"
    for name in ("android_guest_setup.generated.h", "android_gpu_early.generated.h"):
        (output / name).write_bytes((generated / name).read_bytes())
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
    manifest = {
        "guest_kernel_sha256": KERNEL,
        "guest_initramfs_sha256": RAMDISK,
        "build_starts_vm": False,
        "session_seconds": 1800 if product else 180,
        "inputs": {name: digest(path) for name, path in inputs.items()},
        "runtime": {
            name: digest(output / name)
            for name in (
                "crosvm",
                "adopt-vmm",
                "manager",
                "policy",
                "allocation-probe",
                "pressure-check",
            )
        },
    }
    (output / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if args.profile_hashes:
        hashes = json.loads(args.profile_hashes.read_text())
        names = ("android-work.qcow2", "android-misc.raw", "android-frp.raw", "android-lowram.cpio")
        if set(hashes) != set(names) or any(
            not re.fullmatch("[0-9a-f]{64}", value) for value in hashes.values()
        ):
            raise ValueError("Expected exactly the four offline profile SHA-256 values")
        header = "static const struct copy_item profile_items[] = {\n"
        header += "".join('{"' + name + '", "' + hashes[name] + '"},\n' for name in names) + "};\n"
        header += "static const struct copy_item runtime_items[] = {\n"
        header += (
            "".join(
                '{"' + name + '", "' + value + '"},\n'
                for name, value in manifest["runtime"].items()
            )
            + "};\n"
        )
        (output / "android_surface_profile.generated.h").write_text(header)
        subprocess.run(
            base
            + [str(ROOT / "tools/android_surface_profile.c"), "-o", str(output / "profile-copy")],
            check=True,
        )
    if product:
        import shutil

        assets = args.product_assets
        assets.mkdir(parents=True, exist_ok=True)
        payloads = {name: output / name for name in manifest["runtime"]}
        payloads.update(
            {
                "guest.Image": assets / "gpu4g-guest.Image",
                "libcrosvm-surface.so": library,
                "libsurface-probe.so": jni,
                "surface-agent.jar": code,
                "network-controller": assets / "gpu4g-network-controller",
            }
        )
        for name, source in payloads.items():
            shutil.copyfile(source, assets / ("gpu4g-surface-" + name))
        (output / "gpu4g_install.generated.h").write_text(
            "static const struct gpu4g_artifact gpu4g_artifacts[] = {\n"
            + "".join(
                '{"gpu4g-surface-' + name + '", "' + name + '", "' + digest(source) + '"},\n'
                for name, source in payloads.items()
            )
            + "};\n"
        )
        subprocess.run(
            base
            + [
                "-DPLB_SURFACE_PRODUCT=1",
                str(ROOT / "native/android_gpu4g_launcher.c"),
                "-o",
                str(assets / "android-gpu4g-surface-bridge"),
            ],
            check=True,
        )
    print("Built separate Surface runtime; no device operation")


if __name__ == "__main__":
    main()
