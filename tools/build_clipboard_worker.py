#!/usr/bin/env python3
"""Compile only the opt-in Java clipboard helper; no kernel or platform changes."""

from pathlib import Path
import subprocess, zipfile


def build(root: Path, sdk: Path, out: Path):
    dest = root / "app/build/native/clipboard"
    classes = dest / "classes"
    dex = dest / "dex"
    classes.mkdir(parents=True, exist_ok=True)
    dex.mkdir(exist_ok=True)
    jar = sdk / "platforms/android-37.0/android.jar"
    if not jar.is_file():
        jar = sdk / "platforms/android-37/android.jar"
    subprocess.run(
        [
            "javac",
            "--release",
            "11",
            "-cp",
            str(jar),
            "-d",
            str(classes),
            str(root / "native/clipboard/ClipboardWorker.java"),
        ],
        check=True,
    )
    d8 = sdk / "build-tools/36.1.0/d8"
    subprocess.run(
        [
            str(d8),
            "--min-api",
            "36",
            "--lib",
            str(jar),
            "--output",
            str(dex),
            *map(str, classes.rglob("*.class")),
        ],
        check=True,
    )
    archive = dest / "worker-unaligned.jar"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_STORED) as jar:
        jar.write(dex / "classes.dex", "classes.dex")
    subprocess.run(
        [
            str(sdk / "build-tools/36.1.0/zipalign"),
            "-f",
            "4",
            str(archive),
            str(out / "clipboard-worker.jar"),
        ],
        check=True,
    )
    (out / "clipboard-worker.dex").unlink(missing_ok=True)
