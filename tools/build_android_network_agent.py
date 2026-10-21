#!/usr/bin/env python3
"""Compile the guest NetworkAgent against checked signatures, excluding all stubs."""

import argparse
from pathlib import Path
import subprocess
import shutil
import os
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    args = parser.parse_args()
    source = ROOT / "tools/android_guest_network"
    output = ROOT / "artifacts/android4g-gpu/network-agent"
    stubs, classes, dex = [output / name for name in ("stubs", "classes", "dex")]
    for directory in (stubs, classes, dex):
        directory.mkdir(parents=True, exist_ok=True)
    sdk = args.sdk / "platforms/android-37.0/android.jar"
    java = (
        Path(os.environ["JAVA_HOME"]) / "bin"
        if "JAVA_HOME" in os.environ
        else Path(shutil.which("javac")).parent
    )
    subprocess.run(
        [
            str(java / "javac"),
            "--release",
            "17",
            "-cp",
            str(sdk),
            "-d",
            str(stubs),
            *map(str, (source / "stubs").rglob("*.java")),
        ],
        check=True,
    )
    subprocess.run(
        [
            str(java / "javac"),
            "--release",
            "17",
            "-cp",
            f"{stubs}:{sdk}",
            "-d",
            str(classes),
            str(source / "GuestNetworkAgent.java"),
            str(source / "GuestConnectivityProbe.java"),
        ],
        check=True,
    )
    built = list(classes.rglob("*.class"))
    if not built or any("android/net" in str(path.relative_to(classes)) for path in built):
        raise RuntimeError("Only first-party guest classes may be packaged")
    subprocess.run(
        [
            str(args.sdk / "build-tools/36.1.0/d8"),
            "--release",
            "--min-api",
            "36",
            "--lib",
            str(sdk),
            "--classpath",
            str(stubs),
            "--output",
            str(dex),
            *map(str, built),
        ],
        check=True,
    )
    with zipfile.ZipFile(output / "agent.jar", "w") as archive:
        archive.writestr(
            zipfile.ZipInfo("classes.dex", date_time=(1980, 1, 1, 0, 0, 0)),
            (dex / "classes.dex").read_bytes(),
        )
    print(output / "agent.jar")


if __name__ == "__main__":
    main()
