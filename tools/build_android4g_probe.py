#!/usr/bin/env python3
"""Build a separate 4 GiB adopted-RAM Android GLES probe; never install/start it."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import shlex
import subprocess

from build_android_gpu_probe import gles_ramdisk, early_command
from build_guest_gpu_payload import build as build_payload

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--vmm", type=Path, required=True)
    parser.add_argument("--build-manifest", type=Path, required=True)
    parser.add_argument("--guest-render-apk", type=Path, required=True)
    parser.add_argument("--minimum-folio-kib", type=int, choices=(16, 128), default=16)
    parser.add_argument("--transport", choices=("pipe", "asg"), default="pipe")
    args = parser.parse_args()
    if shutil.disk_usage(ROOT).free < 21 * 1024**3:
        parser.error("Retain 20 GiB Mac storage headroom")
    manifest = json.loads(args.build_manifest.read_text())
    expected = {"gunyah", "plb-preallocated-ram", "gpu", "gfxstream", "qcow", "android-sparse"}
    if (
        manifest.get("vmm_sha256") != digest(args.vmm)
        or set(manifest.get("features", [])) != expected
        or manifest.get("host_library_sha256", {}).get("libgfxstream_backend.so")
        != "41414d640a526f56ce9a151f786e965a6e1fe3769e201790e0e58e7a31ff87da"
        or manifest.get("source_revision") != "8a4abfbaea5567de24e49cc43874c820098987d0"
        or manifest.get("minijail_revision") != "c6fe710936b84753b99e6d0e9ea634109272f307"
        or any(
            manifest.get("patch_sha256", {}).get(name) != digest(ROOT / "tools" / name)
            for name in (
                "crosvm_preallocated_ram.rs",
                "crosvm_split_lend.rs",
                "crosvm_preallocated_ram.patch",
                "crosvm_android_headless.patch",
                "crosvm_gpu_diagnostics.patch",
            )
        )
    ):
        parser.error("Use the pinned separate Android GPU VMM build")
    out = ROOT / "artifacts/android4g-gpu/probe-build"
    out.mkdir(mode=0o700, parents=True, exist_ok=True)
    ramdisk = out / "android-lowram.cpio"
    ramdisk.write_bytes(
        gles_ramdisk(
            (ROOT / "artifacts/android-display/cuttlefish17/android-lowram.cpio").read_bytes()
        )
    )
    pins = {
        "VMM": digest(args.vmm),
        "KERNEL": "f722bdd809bfd3485917a564b053b8a6d4938408734091f420c3297a8e3b20c8",
        "INITRAMFS": digest(ramdisk),
    }
    (out / "preallocated_vm.generated.h").write_text(
        "/* Generated private artifact identities. */\n"
        + "\n".join(
            f'#define PLB_PREALLOCATED_{key}_SHA256 "{value}"' for key, value in pins.items()
        )
        + "\n"
    )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    flags = [str(compiler), "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(out)]
    if args.transport == "asg":
        flags.append("-DPLB_ANDROID4G_ASG=1")
    proxy = out / "android4g-adopt-vmm"
    subprocess.run(
        flags
        + [
            "-DPLB_PREALLOCATED_ANDROID_PROXY=1",
            str(ROOT / "tools/preallocated_vm_probe.c"),
            "-o",
            str(proxy),
        ],
        check=True,
    )
    (out / "android4g.generated.h").write_text(
        f'#define PLB_ANDROID4G_VMM_SHA256 "{pins["VMM"]}"\n'
        f'#define PLB_ANDROID4G_PROXY_SHA256 "{digest(proxy)}"\n'
    )
    setup = (ROOT / "native/android_guest_setup.sh").read_text()
    (out / "android_guest_setup.generated.h").write_text(
        "static const char android_setup_service[] =\n"
        + json.dumps("exec:sh -c " + shlex.quote(setup))
        + ";\n"
    )
    (out / "android_gpu_early.generated.h").write_text(
        "static const char console_swap_command[] =\n"
        + json.dumps(early_command((ROOT / "tools/android_gpu_early_setup.sh").read_text()))
        + ";\n"
    )
    apk_sha = build_payload(args.guest_render_apk, args.sdk, out / "android_gpu_probe.generated.h")
    manager = out / "android4g-manager"
    subprocess.run(
        flags
        + [
            "-pthread",
            "-DPLB_ANDROID_ADOPTED_PROBE=1",
            "-DPLB_ANDROID_GFXSTREAM_PROBE=1",
            "-DPLB_ANDROID_GPU_RENDER_PROBE=1",
            str(ROOT / "tools/android_probe_runner.c"),
            "-o",
            str(manager),
        ],
        check=True,
    )
    controller = out / "android4g-policy"
    subprocess.run(
        flags
        + [
            "-DPLB_PREALLOCATED_VM_POLICY=1",
            "-DPLB_MEDIUM_SHMEM_PROPOSAL=1",
            *(["-DPLB_ANDROID4G_MEDIUM_ONLY=1"] if args.minimum_folio_kib == 128 else []),
            str(ROOT / "tools/small_shmem_policy_probe.c"),
            "-o",
            str(controller),
        ],
        check=True,
    )
    copy = out / "android4g-profile-copy"
    subprocess.run(
        flags + [str(ROOT / "tools/android4g_profile_copy.c"), "-o", str(copy)], check=True
    )
    diagnostics = out / "android4g-guest-diagnostics"
    subprocess.run(
        flags + [str(ROOT / "tools/android4g_guest_diagnostics.c"), "-o", str(diagnostics)],
        check=True,
    )
    record = {
        "configured_memory_mib": 4096,
        "guest_graphics_transport": "virtio-gpu-" + args.transport,
        "advised_shmem_sizes_kib": (
            [16, 32, 64, 128, 256, 512] if args.minimum_folio_kib == 16 else [128, 256, 512]
        ),
        "backing_preparation_order": "ascending",
        "backing_mapping_alignment_bytes": 2097152,
        "gpu_bar_bytes": 268435456,
        "combined_explicit_budget_limit_mib": 6144,
        "host_policy_max_ms": 40000,
        "vmm_pins": pins,
        "guest_render_apk_sha256": apk_sha,
        "files_sha256": {
            p.name: digest(p)
            for p in (
                proxy,
                manager,
                controller,
                ramdisk,
                copy,
                diagnostics,
                ROOT / "tools/prepare_android4g_profile.sh",
            )
        },
        "vm_started_by_build": False,
        "production_apk_changed": False,
    }
    (out / "build.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
