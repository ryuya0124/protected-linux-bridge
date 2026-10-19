#!/usr/bin/env python3
"""Build the private renderer investigation without any Android device access.

Requires a dedicated shallow gfxstream checkout and read-only link inputs from
the reviewed host build. It never installs a library or starts a VM. Guest VM
execution is separately disabled after the observed host restart.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REVISION = "7e9d595b097b23f5fa5c114088245c819e29a9af"
PLATFORM_HASHES = {
    "libnativewindow.so": "0421e94d0d97c8d8570fd079f063cfe34f7a059d9a065160c646c3e60c027598",
    "libcutils.so": "4b74649dfff9689b27c955ddd85cac580330e50bb0b7d76dad15561230d2bffd",
    "cutils/native_handle.h": "af0277ffd3756c4c1047071742c362ef1b3f036552eefad98980266fc4660e9c",
    "vndk/hardware_buffer.h": "1aa7ff7f91dd67e1536b90ed63c57a232e7f0f5a3a16ab42b51645a590240dd6",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--platform-headers", type=Path, required=True)
    parser.add_argument("--platform-libs", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--jobs", type=int, choices=range(1, 5), default=4)
    args = parser.parse_args()
    output = ROOT / "artifacts/android-vulkan/gfxstream-build"
    patch = ROOT / "tools/gfxstream_android_vulkan.patch"
    if shutil.disk_usage(ROOT).free < 1024**3:
        raise ValueError("Insufficient Mac storage for this owned build")
    source = args.source.resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != REVISION:
        raise ValueError("Use the pinned dedicated gfxstream checkout")
    difference = subprocess.check_output(
        ["git", "-C", str(source), "diff", "--binary", "--no-ext-diff"]
    )
    if not difference:
        subprocess.run(["git", "-C", str(source), "apply", "--check", str(patch)], check=True)
        subprocess.run(["git", "-C", str(source), "apply", str(patch)], check=True)
    elif difference != patch.read_bytes():
        raise ValueError("Dedicated source contains changes outside the reviewed patch")
    for name, expected in PLATFORM_HASHES.items():
        path = (args.platform_headers if "/" in name else args.platform_libs) / name
        if path.is_symlink() or not path.is_file() or digest(path) != expected:
            raise ValueError("Unexpected platform link input: " + name)
    sdk = args.sdk.resolve()
    subprocess.run(
        [
            args.cmake,
            "-S",
            str(source),
            "-B",
            str(output),
            "-G",
            "Ninja",
            "-DCMAKE_MAKE_PROGRAM=" + str(sdk / "cmake/3.22.1/bin/ninja"),
            "-DCMAKE_TOOLCHAIN_FILE="
            + str(sdk / "ndk/28.2.13676358/build/cmake/android.toolchain.cmake"),
            "-DANDROID_ABI=arm64-v8a",
            "-DANDROID_PLATFORM=android-35",
            "-DANDROID_STL=c++_static",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DENABLE_VKCEREAL_TESTS=OFF",
            "-DGFXSTREAM_ENABLE_HOST_TRACING=OFF",
            "-DPLB_ANDROID_PLATFORM_HEADERS=" + str(args.platform_headers.resolve()),
            "-DPLB_ANDROID_NATIVEWINDOW_LIBRARY="
            + str((args.platform_libs / "libnativewindow.so").resolve()),
            "-DPLB_ANDROID_CUTILS_LIBRARY=" + str((args.platform_libs / "libcutils.so").resolve()),
        ],
        check=True,
    )
    subprocess.run(
        [args.cmake, "--build", str(output), "--target", "gfxstream_backend", "-j", str(args.jobs)],
        check=True,
    )
    import platform

    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    strip = sdk / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / host / "bin/llvm-strip"
    artifact = output.parent / "libgfxstream-vulkan.so"
    subprocess.run(
        [str(strip), "--strip-debug", "-o", str(artifact), str(output / "libgfxstream_backend.so")],
        check=True,
    )
    (output.parent / "renderer-build.json").write_text(
        json.dumps(
            {
                "source_revision": REVISION,
                "patch_sha256": digest(patch),
                "renderer_sha256": digest(artifact),
                "platform_link_inputs": PLATFORM_HASHES,
                "build_starts_vm": False,
                "host_files_modified": False,
                "guest_vulkan_validated": False,
                "guest_vm_execution_disabled_after_host_restart": True,
            },
            indent=2,
        )
        + "\n"
    )
    print("Built private renderer only; guest Vulkan remains unvalidated and VM execution disabled")


if __name__ == "__main__":
    main()
