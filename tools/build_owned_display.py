#!/usr/bin/env python3
"""Build the private Android 17 display backend and publisher. Never start a VM.

The artifact is a replacement for the new VMM's display client, not an ELF patch
or an interposer to inject into the system crosvm. No VMM is built by this tool.
"""

from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def build(sdk, output):
    output = output.resolve()
    if not (
        output.is_relative_to((ROOT / "artifacts").resolve())
        or output.is_relative_to((ROOT / "app/build").resolve())
    ):
        raise ValueError("Use a dedicated build/artifacts output directory")
    output.mkdir(parents=True, exist_ok=True)
    generated = output / "generated"
    aidl = ROOT / "native/owned_display/aidl"
    subprocess.run(
        [
            str(sdk / "build-tools/36.1.0/aidl"),
            "--lang=ndk",
            "-I",
            str(aidl),
            "-I",
            str(ROOT / "native/owned_display/platform"),
            "-h",
            str(generated / "include"),
            "-o",
            str(generated),
            str(aidl / "net/ryuya_dev/protectedlinux/display/IOwnedDisplay.aidl"),
        ],
        check=True,
    )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang++"
    )
    backend = output / "libplb_owned_display_backend.so"
    subprocess.run(
        [
            str(compiler),
            "-shared",
            "-fPIC",
            "-static-libstdc++",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-std=c++17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wl,--no-undefined",
            "-Wl,--exclude-libs,ALL",
            "-Wl,--strip-debug",
            "-Wl,-soname,libplb_owned_display_backend.so",
            "-I",
            str(ROOT / "third_party/android-binder"),
            "-I",
            str(generated / "include"),
            "-I",
            str(ROOT / "native/owned_display"),
            str(ROOT / "native/owned_display/backend.cpp"),
            str(generated / "net/ryuya_dev/protectedlinux/display/IOwnedDisplay.cpp"),
            "-lbinder_ndk",
            "-lnativewindow",
            "-landroid",
            "-ldl",
            "-llog",
            "-o",
            str(backend),
        ],
        check=True,
    )
    sha = hashlib.sha256(backend.read_bytes()).hexdigest()
    compatibility = json.loads((ROOT / "compatibility/android17-s948q-bzi9.json").read_text())[
        "host"
    ]
    sources, classes, dex = [output / name for name in ["sources", "classes", "dex"]]
    for folder in [sources, classes, dex]:
        folder.mkdir(exist_ok=True)
    (sources / "OwnedDisplayBuild.java").write_text(
        "package net.ryuya_dev.protectedlinux.displayroot;\nfinal class OwnedDisplayBuild {\n"
        + "".join(
            f"  static final String {key} = {json.dumps(value)};\n"
            for key, value in {
                "BACKEND_SHA256": sha,
                "MODEL": compatibility["model"],
                "BUILD": compatibility["build"],
                "KERNEL": compatibility["kernel_release"],
            }.items()
        )
        + "}\n"
    )
    java = (
        Path(os.environ["JAVA_HOME"]) / "bin"
        if "JAVA_HOME" in os.environ
        else Path(shutil.which("javac")).parent
    )
    android = sdk / "platforms/android-37.0/android.jar"
    subprocess.run(
        [
            str(java / "javac"),
            "--release",
            "17",
            "-cp",
            str(android),
            "-d",
            str(classes),
            str(sources / "OwnedDisplayBuild.java"),
            str(
                ROOT
                / "native/owned_display/root/net/ryuya_dev/protectedlinux/displayroot/RootOwnedDisplayPublisher.java"
            ),
            str(
                ROOT
                / "app/src/main/java/net/ryuya_dev/protectedlinux/display/DisplayCapability.java"
            ),
        ],
        check=True,
    )
    # DEX instruction minimum only; publisher explicitly admits SDK 37, and the
    # enclosing APK is minSdk 37. Build-tools 36.1's D8 cannot model API 37 yet.
    subprocess.run(
        [
            str(sdk / "build-tools/36.1.0/d8"),
            "--release",
            "--min-api",
            "36",
            "--lib",
            str(android),
            "--output",
            str(dex),
            *map(str, classes.rglob("*.class")),
        ],
        check=True,
    )
    publisher = output / "owned-display-publisher.jar"
    with zipfile.ZipFile(publisher, "w") as archive:
        archive.writestr(
            zipfile.ZipInfo("classes.dex", date_time=(1980, 1, 1, 0, 0, 0)),
            (dex / "classes.dex").read_bytes(),
        )
    manifest = {
        "protocol": 1,
        "backend_sha256": sha,
        "publisher_sha256": hashlib.sha256(publisher.read_bytes()).hexdigest(),
        "global_display_service_used": False,
        "vm_built": False,
        "device_tested": False,
    }
    (output / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return backend, publisher


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.sdk, args.output)
