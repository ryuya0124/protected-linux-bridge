#!/usr/bin/env python3
"""Build a shell-only Terminal reflection probe. No ADB invocation or device mutation."""

import argparse
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def build(sdk):
    output = ROOT / "artifacts/android17-host/terminal-contract-probe"
    classes, dex = output / "classes", output / "dex"
    classes.mkdir(parents=True, exist_ok=True)
    dex.mkdir(exist_ok=True)
    android = sdk / "platforms/android-37.0/android.jar"
    java = Path(os.environ["JAVA_HOME"]) / "bin/javac"
    source = ROOT / "app/src/main/java/net/ryuya_dev/protectedlinux"
    subprocess.run(
        [
            str(java),
            "--release",
            "17",
            "-cp",
            str(android),
            "-d",
            str(classes),
            str(source / "TerminalContract.java"),
            str(source / "HostContract.java"),
            str(ROOT / "tools/terminal_probe/TerminalContractProbe.java"),
        ],
        check=True,
    )
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
    jar = output / "terminal-contract-probe.jar"
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
