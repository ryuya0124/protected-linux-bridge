#!/usr/bin/env python3
"""Build the isolated Android guest GPU diagnostic; never install or launch it.

All output is private development data. The default 512 MiB GLES path uses the original
installed crosvm, with no preload, private VMM copy or global host-policy writes.
The separately selected Vulkan diagnostic pins its process-local compatibility
library and remains unverified. Existing guest disks are never opened here.
The 1536 MiB GLES build adds a separate prefault-only memory library and bounded
three-key policy supervisor. It is not boot-validated and never runs here.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shlex
import shutil
import stat
import subprocess

from android_ramdisk import entries, PROPERTY_PATH
from build_guest_gpu_payload import build as build_guest_payload

ROOT = Path(__file__).resolve().parents[1]
LOWRAM_SHA256 = "7e0c04dd3f3b9b92398c1e5ac0161e93d78c450e857d530d55012a6a550315e2"
GPU_RAMDISK_SHA256 = "ebf7141a7d287fdd877065109b5e68dfa9339a5a460c8d5795ff7355a36dd143"


def gles_ramdisk(original):
    if hashlib.sha256(original).hexdigest() != LOWRAM_SHA256:
        raise ValueError("The original low-RAM initramfs does not match the pinned profile")
    properties = next(
        payload
        for name, mode, payload in reversed(list(entries(original)))
        if name == PROPERTY_PATH
    )
    properties += (
        b"\n# Guest-only GLES path, no Vulkan renderer requirement\n"
        b"debug.renderengine.backend=skiaglthreaded\ndebug.hwui.renderer=skiagl\n"
    )
    extra = bytearray()
    for inode, name, mode, payload in (
        (100003, PROPERTY_PATH, stat.S_IFREG | 0o644, properties),
        (100004, "TRAILER!!!", 0, b""),
    ):
        encoded = name.encode() + b"\0"
        fields = [inode, mode, 0, 0, 1, 0, len(payload), 0, 0, 0, 0, len(encoded), 0]
        extra.extend(b"070701" + "".join(f"{v:08x}" for v in fields).encode() + encoded)
        extra.extend(b"\0" * (-len(extra) % 4))
        extra.extend(payload)
        extra.extend(b"\0" * (-len(extra) % 4))
    result = original + extra
    if hashlib.sha256(result).hexdigest() != GPU_RAMDISK_SHA256:
        raise ValueError("Unexpected GPU initramfs output")
    return result


def early_command(script):
    # The non-root console must identify the pinned guest before invoking its
    # original userdebug su. The bootstrap repeats those checks after elevation.
    return (
        '\nif [ "$(getprop ro.product.device)" = vsoc_arm64_only ] && '
        '[ "$(getprop ro.hardware)" = cutf_cvm ] && '
        '[ "$(getprop ro.build.version.incremental)" = 16373615 ]; then '
        "su 0 sh -c " + shlex.quote(script) + "; fi\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--mode", choices=("gles", "vulkan"), default="gles")
    parser.add_argument("--memory", type=int, choices=(512, 1536), default=512)
    parser.add_argument(
        "--guest-render-apk",
        type=Path,
        help="Embed only the signed fixed-package guest GLES probe (1536 MiB only)",
    )
    parser.add_argument(
        "--approved-warning-boot-id",
        help="Explicit one-trial approval only; private current boot UUID",
    )
    parser.add_argument(
        "--approved-warning-issued-ms",
        type=int,
        help="Current host CLOCK_BOOTTIME milliseconds; authorization expires in five minutes",
    )
    args = parser.parse_args()
    warning_once = args.approved_warning_boot_id is not None
    if warning_once != (args.approved_warning_issued_ms is not None):
        parser.error("Both one-trial approval fields are required")
    if warning_once and (
        not args.guest_render_apk
        or args.memory != 1536
        or args.mode != "gles"
        or not re.fullmatch(
            r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}", args.approved_warning_boot_id
        )
        or args.approved_warning_issued_ms <= 0
    ):
        parser.error(
            "Approved affected-boot trial is restricted to the fixed 1536 MiB GLES render test"
        )
    if args.memory == 1536 and args.mode != "gles":
        parser.error("The larger diagnostic currently supports GLES only")
    if args.guest_render_apk and args.memory != 1536:
        parser.error("The guest render APK requires the guarded 1536 MiB GLES build")
    if shutil.disk_usage(ROOT).free < 21 * 1024**3:
        parser.error("Retain at least 20 GiB Mac storage headroom")
    label = args.mode if args.memory == 512 else "gles1536"
    if args.guest_render_apk:
        label = "gles1536-render-once" if warning_once else "gles1536-render"
    out = ROOT / "artifacts/android-display/gpu-research" / label
    out.mkdir(mode=0o700, parents=True, exist_ok=True)
    once_flags = []
    if warning_once:
        (out / "gpu_once.generated.h").write_text(
            "/* Private, explicit one-trial approval. Never commit this generated file. */\n"
            + "#define PLB_GPU_ONCE_BOOT_ID "
            + json.dumps(args.approved_warning_boot_id)
            + "\n"
            + f"#define PLB_GPU_ONCE_ISSUED_MS {args.approved_warning_issued_ms}ULL\n"
            + f"#define PLB_GPU_ONCE_DEADLINE_MS {args.approved_warning_issued_ms + 300000}ULL\n"
            + "#define PLB_GPU_ONCE_TAINT 4640UL\n"
        )
        once_flags = ["-DPLB_GPU_WARNING_ONCE=1", "-I", str(out)]
    original = ROOT / "artifacts/android-display/cuttlefish17/android-lowram.cpio"
    (out / "android-gles.cpio").write_bytes(gles_ramdisk(original.read_bytes()))
    setup = (ROOT / "native/android_guest_setup.sh").read_text()
    supplement = (ROOT / "tools/android_gpu_early_setup.sh").read_text()
    (out / "android_gpu_early.generated.h").write_text(
        "/* Generated guest-only diagnostic command. */\n"
        + "static const char console_swap_command[] =\n"
        # In the larger probe the identity-checked ADB observer owns swap setup.
        # Avoid two threads racing its zram initialization through the console.
        + json.dumps(early_command((setup + "\n" if args.memory == 512 else "") + supplement))
        + ";\n"
    )
    (out / "android_guest_setup.generated.h").write_text(
        "/* Generated from native/android_guest_setup.sh. */\n"
        + "static const char android_setup_service[] =\n"
        + json.dumps("exec:sh -c " + shlex.quote(setup))
        + ";\n"
    )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    flags = [str(compiler), "-O2", "-Wall", "-Wextra", "-Werror"]
    defines = ["-DPLB_ANDROID_GFXSTREAM_PROBE=1"] + once_flags
    sources = [
        ROOT / "native/android_guest_manager.c",
        ROOT / "native/android_guest_setup.sh",
        ROOT / "native/android_guest_adb.h",
        ROOT / "native/android_guest_display.h",
        ROOT / "native/android_sensor_transport.h",
        ROOT / "native/profile_lock.h",
        ROOT / "tools/android_probe_runner.c",
        ROOT / "tools/android_gpu_console.h",
        ROOT / "tools/android_gpu_early_setup.sh",
        ROOT / "tools/build_android_gpu_probe.py",
        ROOT / "tools/android_ramdisk.py",
        ROOT / "tools/gfxstream_loader_probe.c",
    ]
    guest_apk_digest = None
    if args.guest_render_apk:
        guest_apk_digest = build_guest_payload(
            args.guest_render_apk, args.sdk, out / "android_gpu_probe.generated.h"
        )
        defines.append("-DPLB_ANDROID_GPU_RENDER_PROBE=1")
        sources += [
            ROOT / "tools/android_gpu_render_probe.h",
            ROOT / "tools/build_guest_gpu_payload.py",
        ]
    executable = out / "android-gpu-runner"
    if args.memory == 1536:
        executable = out / (
            "android-gpu1536-gles-once-runner" if warning_once else "android-gpu1536-gles-runner"
        )
        library = out / (
            "libandroid_gpu1536_gles_once_backing.so"
            if warning_once
            else "libandroid_gpu1536_gles_backing.so"
        )
        subprocess.run(
            flags
            + once_flags
            + [
                "-shared",
                "-fPIC",
                "-DPLB_ANDROID_GPU_BACKING=1",
                str(ROOT / "tools/memory_backing_probe.c"),
                "-o",
                str(library),
            ],
            check=True,
        )
        digest = hashlib.sha256(library.read_bytes()).hexdigest()
        defines += [
            "-DPLB_ANDROID_MEMORY_PROBE=1",
            "-DPLB_ANDROID_MEMORY_MIB=1536",
            "-DPLB_ANDROID_MEMORY_POLICY=1",
            "-DPLB_ANDROID_GPU_MEMORY_PROBE=1",
            f'-DPLB_ANDROID_MEMORY_LIBRARY_SHA256="{digest}"',
        ]
        sources += [
            ROOT / name
            for name in (
                "tools/android_gpu_memory_config.h",
                "tools/memory_backing_probe.c",
                "tools/android_memory_observer.h",
                "tools/owned_page_summary.h",
                "tools/host_memory_health.h",
                "tools/diagnostic_owner_lock.h",
                "tools/small_shmem_policy_probe.c",
            )
        ]
        if warning_once:
            sources += [ROOT / "tools/gpu_warning_once.h"]
        supervisor = out / (
            "small-shmem-policy-probe-gpu-once" if warning_once else "small-shmem-policy-probe"
        )
        subprocess.run(
            flags
            + once_flags
            + [
                str(ROOT / "tools/small_shmem_policy_probe.c"),
                "-o",
                str(supervisor),
            ],
            check=True,
        )
    if args.mode == "vulkan":
        library = out / "libgfxstream_android_compat.so"
        source = ROOT / "tools/gfxstream_android_compat.c"
        subprocess.run(
            flags
            + [
                "-shared",
                "-fPIC",
                "-DPLB_GFXSTREAM_DIAGNOSTIC_LOG=1",
                str(source),
                "-ldl",
                "-o",
                str(library),
            ],
            check=True,
        )
        digest = hashlib.sha256(library.read_bytes()).hexdigest()
        defines += [
            "-DPLB_ANDROID_GFXSTREAM_VULKAN_PROBE=1",
            f'-DPLB_GFXSTREAM_COMPAT_SHA256="{digest}"',
        ]
        sources.append(source)
    subprocess.run(
        flags
        + defines
        + [
            "-I",
            str(out),
            str(ROOT / "tools/android_probe_runner.c"),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run(
        flags
        + [
            str(ROOT / "tools/gfxstream_loader_probe.c"),
            "-ldl",
            "-o",
            str(out / "gfxstream-loader-probe"),
        ],
        check=True,
    )
    records = {
        str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sources
        + list(out.glob("*.h"))
        + list(out.glob("*.so"))
        + [out / "android-gles.cpio", executable, out / "gfxstream-loader-probe"]
        + ([supervisor] if args.memory == 1536 else [])
    }
    (out / "build.json").write_text(
        json.dumps(
            {
                "mode": args.mode,
                "memory_mib": args.memory,
                "boot_validated": False,
                "explicit_affected_boot_single_trial": warning_once,
                "guest_render_apk_sha256": guest_apk_digest,
                "sha256": records,
            },
            indent=2,
        )
        + "\n"
    )
    print(f"Built only: {out}. No device operation, VM launch or host setting change.")


if __name__ == "__main__":
    main()
