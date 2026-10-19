#!/usr/bin/env python3
"""Build the isolated, short-lived GPU Surface prerequisite probe. No device I/O."""

import argparse
import hashlib
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    args = parser.parse_args()
    output = ROOT / "artifacts/android4g-gpu/surface-probe"
    output.mkdir(parents=True, exist_ok=True)
    sources = ROOT / "tools/surface_probe"
    java = output / "src/main/java/net/ryuya_dev/protectedlinux/surfaceprobe"
    java.mkdir(parents=True, exist_ok=True)
    for path in sources.glob("*.java"):
        shutil.copy2(path, java / path.name)
    shutil.copy2(sources / "AndroidManifest.xml", output / "src/main/AndroidManifest.xml")
    for name in ("build.gradle.kts", "settings.gradle.kts", "proguard-rules.pro"):
        shutil.copy2(sources / name, output / name)
    (output / "local.properties").write_text("sdk.dir=" + str(args.sdk.resolve()) + "\n")
    (output / "gradle.properties").write_text(
        "org.gradle.jvmargs=-Xmx1024m -Dfile.encoding=UTF-8\norg.gradle.parallel=false\n"
    )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    subprocess.run(
        [
            str(compiler),
            "-shared",
            "-fPIC",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(sources / "surface_probe.c"),
            "-lEGL",
            "-lGLESv2",
            "-landroid",
            "-ldl",
            "-lnativehelper",
            "-Wl,--no-undefined",
            "-o",
            str(output / "libsurface-probe.so"),
        ],
        check=True,
    )
    library = (
        ROOT
        / "artifacts/android4g-gpu/vmm-build/target/aarch64-linux-android/release/libplb_crosvm_surface.so"
    )
    digest = (
        hashlib.sha256(library.read_bytes()).hexdigest() if library.is_file() else "unavailable"
    )
    (java / "RootVmmLoadProbe.java").write_text(
        (sources / "RootVmmLoadProbe.java").read_text().replace("@VMM_SHA256@", digest)
    )
    subprocess.run([str(ROOT / "gradlew"), "-p", str(output), "assembleRelease"], check=True)
    print(output / "build/outputs/apk/release/PlbSurfaceProbe-release.apk")


if __name__ == "__main__":
    main()
