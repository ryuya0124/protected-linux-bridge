#!/usr/bin/env python3
"""Build the pinned root-only Surface launcher DEX and JNI; no Android device I/O.

Reuse the measured prototype's root entry points, with a fixed Bridge provider
and private runtime. The archive has no exported Activity or executable API.
"""

import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = "/data/user/0/net.ryuya_dev.protectedlinux/files/android-gpu4g-surface-runtime/"
LIBRARY_SHA = "3e709c50a002e480c887aa2c337b067b850069f58ee05d72732654fb8b509e13"


def build(sdk, output, library):
    if hashlib.sha256(library.read_bytes()).hexdigest() != LIBRARY_SHA:
        raise ValueError("Surface VMM must match the separately measured library")
    sources = output / "agent-sources"
    classes = output / "agent-classes"
    dex = output / "agent-dex"
    for directory in (sources, classes, dex):
        directory.mkdir(parents=True, exist_ok=True)
    for name in ("RootSurfaceAccess.java", "RootVmmLoadProbe.java", "RootSurfaceVmm.java"):
        text = (ROOT / "tools/surface_probe" / name).read_text()
        text = text.replace(
            'String authority = "net.ryuya_dev.protectedlinux.surfaceprobe";',
            'String authority = "net.ryuya_dev.protectedlinux.gpu-surface";',
        )
        text = text.replace(
            "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/", RUNTIME
        ).replace("@VMM_SHA256@", LIBRARY_SHA)
        (sources / name).write_text(text)
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
            *map(str, sources.glob("*.java")),
        ],
        check=True,
    )
    subprocess.run(
        [
            str(sdk / "build-tools/36.0.0/d8"),
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
    with zipfile.ZipFile(output / "surface-agent.jar", "w") as archive:
        archive.writestr(
            zipfile.ZipInfo("classes.dex", date_time=(1980, 1, 1, 0, 0, 0)),
            (dex / "classes.dex").read_bytes(),
        )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        sdk
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
            '-DPLB_SURFACE_VMM_LIBRARY="' + RUNTIME + 'libcrosvm-surface.so"',
            str(ROOT / "tools/surface_probe/surface_probe.c"),
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
    return output / "surface-agent.jar", output / "libsurface-probe.so"
