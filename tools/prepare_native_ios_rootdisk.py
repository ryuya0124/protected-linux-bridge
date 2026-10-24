#!/usr/bin/env python3
"""Convert the exact verified main OS DMG to a new private raw image on macOS.

Never mounts it, modifies its source, contacts Android or starts a VM. The only
process group stopped by the space guard is this converter's own new group.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = "3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d"
SOURCE_SHA384 = "d41b9ea8cbe5220f77ff0cfa24d3267aba9ee74a4487551d8c7f1dcc753fcc750e085aedac6fb0f71d875a97f87eb73f"
RAW_BYTES = 6077580800


def digest(path: Path, algorithm="sha256") -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def prepare(source: Path, output: Path) -> dict:
    source = source.resolve(strict=True)
    output = output.resolve()
    private = (ROOT / "artifacts").resolve()
    if not source.is_relative_to(private) or not output.is_relative_to(private) or output.exists():
        raise ValueError("Use an owned verified download and new private output directory")
    report = json.loads((source / "download.private.json").read_text())
    image = source / "RootOS.dmg"
    if (report.get("product"), report.get("build"), report.get("sha256"), report.get("sha384")) != (
        "iPad7,1",
        "21H461",
        SOURCE_SHA256,
        SOURCE_SHA384,
    ) or report.get("both_restore_digests_matched") is not True:
        raise ValueError("Not the verified exact main OS source")
    if image.stat().st_size != 5157406069 or digest(image) != SOURCE_SHA256:
        raise ValueError("Main OS source changed since download verification")
    if shutil.disk_usage(output.parent).free < 8 * 1024**3 + RAW_BYTES + 256 * 1024**2:
        raise ValueError("Insufficient Mac storage for conversion plus8GiB floor")
    output.mkdir()

    def bound_output():
        resource.setrlimit(resource.RLIMIT_FSIZE, (RAW_BYTES + 1024**2, RAW_BYTES + 1024**2))

    with (output / "convert.private.log").open("x") as log:
        process = subprocess.Popen(
            [
                "/usr/bin/hdiutil",
                "convert",
                str(image),
                "-format",
                "UDTO",
                "-o",
                str(output / "RootOS"),
            ],
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
            preexec_fn=bound_output,
        )
        stopped_for_space = False
        while process.poll() is None:
            if shutil.disk_usage(output).free < 8 * 1024**3 + 256 * 1024**2:
                stopped_for_space = True
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                break
            time.sleep(2)
    conversion = {
        "exit": process.returncode,
        "stopped_for_space": stopped_for_space,
        "android_contacted": False,
        "vm_started": False,
    }
    (output / "conversion.private.json").write_text(json.dumps(conversion, indent=2) + "\n")
    if process.returncode or stopped_for_space:
        raise ValueError("Raw conversion failed; preserve its private log")
    raw = output / "RootOS.cdr"
    if raw.stat().st_size != RAW_BYTES:
        raise ValueError("Converted image geometry differs from the original UDIF sector count")
    with raw.open("rb") as file:
        prefix = file.read(4096)
    if len(prefix) != 4096 or prefix[510:512] != b"\x55\xaa" or prefix[512:520] != b"EFI PART":
        raise ValueError("Converted main OS is not the expected GPT disk")
    fnv = 14695981039346656037
    for byte in prefix:
        fnv = ((fnv ^ byte) * 1099511628211) & (2**64 - 1)
    result = {
        "product": "iPad7,1",
        "version": "17.7.11",
        "build": "21H461",
        "source_sha256": SOURCE_SHA256,
        "source_sha384": SOURCE_SHA384,
        "source_both_manifest_digests_verified": True,
        "raw_bytes": RAW_BYTES,
        "raw_sha256": digest(raw),
        "prefix_bytes": 4096,
        "prefix_fnv64": fnv,
        "raw_geometry_matches_udif": True,
        "apple_signature_authentication_qualified": False,
        "android_contacted": False,
        "vm_started": False,
        "ios_boot_verified": False,
    }
    if digest(image) != SOURCE_SHA256:
        raise ValueError("Source image changed during conversion")
    (output / "root-disk.private.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(prepare(args.source, args.output), indent=2))
