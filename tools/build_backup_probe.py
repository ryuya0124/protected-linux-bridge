#!/usr/bin/env python3
"""Build a non-GUI Android backup probe. Does not run ADB or modify device data."""

import argparse
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def build(sdk):
    output = ROOT / "artifacts/android17-host/backup-probe"
    classes, dex = output / "classes", output / "dex"
    classes.mkdir(parents=True, exist_ok=True)
    dex.mkdir(exist_ok=True)
    android = sdk / "platforms/android-37.0/android.jar"
    source = ROOT / "app/src/main/java/net/ryuya_dev/protectedlinux/backup"
    sources = [
        source / (name + ".java")
        for name in ("BackupArchive", "BackupTransaction", "ArchiveTransport", "PreferenceSnapshot")
    ]
    sources.extend((ROOT / "tools/backup_probe").glob("*.java"))
    subprocess.run(
        [
            str(Path(os.environ["JAVA_HOME"]) / "bin/javac"),
            "--release",
            "17",
            "-cp",
            str(android),
            "-d",
            str(classes),
            *map(str, sources),
        ],
        check=True,
    )
    subprocess.run(
        [
            str(sdk / "build-tools/36.1.0/d8"),
            "--release",
            "--min-api",
            "37",
            "--lib",
            str(android),
            "--output",
            str(dex),
            *map(str, classes.rglob("*.class")),
        ],
        check=True,
    )
    jar = output / "backup-probe.jar"
    with zipfile.ZipFile(jar, "w") as archive:
        archive.writestr(
            zipfile.ZipInfo("classes.dex", date_time=(1980, 1, 1, 0, 0, 0)),
            (dex / "classes.dex").read_bytes(),
        )
    print(jar)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    build(parser.parse_args().sdk)
