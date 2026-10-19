#!/usr/bin/env python3
"""Build the developer-only 16 MiB FD handoff probe; never install or run it."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    args = parser.parse_args()
    if shutil.disk_usage(ROOT).free < 21 * 1024**3:
        parser.error("Retain at least 20 GiB Mac storage headroom")
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    output = ROOT / "artifacts/preallocated-ram-handoff"
    output.mkdir(mode=0o700, parents=True, exist_ok=True)
    binary = output / "preallocated-ram-probe"
    source = ROOT / "tools/preallocated_ram_probe.c"
    subprocess.run(
        [str(compiler), "-O2", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
        check=True,
    )
    sources = [
        source,
        ROOT / "tools/preallocated_ram_contract.h",
        ROOT / "tools/host_memory_health.h",
        ROOT / "tools/probe_watchdog.h",
        ROOT / "native/profile_lock.h",
        Path(__file__).resolve(),
    ]
    manifest = {
        "purpose": "fixed 16 MiB ordinary memfd exec handoff; not crosvm/Gunyah import",
        "vm_started": False,
        "host_settings_written": False,
        "sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sources + [binary]
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(binary)


if __name__ == "__main__":
    main()
