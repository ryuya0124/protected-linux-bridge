#!/usr/bin/env python3
"""Build the small backup flock JNI bridge and pin its digest into the APK source."""

from pathlib import Path
import hashlib
import platform
import subprocess


def build(root, sdk, output):
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    clang = (
        sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    library = output / "libbackup-lock.so"
    subprocess.run(
        [
            str(clang),
            "-shared",
            "-fPIC",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fvisibility=hidden",
            str(root / "native/backup/backup_lock.c"),
            "-o",
            str(library),
        ],
        check=True,
    )
    generated = root / "app/build/generated/backupSources/net/ryuya_dev/protectedlinux/backuproot"
    generated.mkdir(parents=True, exist_ok=True)
    sha = hashlib.sha256(library.read_bytes()).hexdigest()
    (generated / "BackupBuild.java").write_text(
        "package net.ryuya_dev.protectedlinux.backuproot;\n"
        "final class BackupBuild {\n"
        f'  static final String LIBRARY_SHA256 = "{sha}";\n'
        "}\n"
    )
