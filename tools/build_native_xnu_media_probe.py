#!/usr/bin/env python3
"""Compile an isolated IOMedia adapter; no device connection, link or VM startup."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from build_native_xnu_probe import build, ROOT


def compile_adapter(source: Path, output: Path, storage_audit: Path, nonce: str) -> dict:
    source = source.resolve(strict=True)
    output = output.resolve()
    storage_audit = storage_audit.resolve(strict=True)
    if not storage_audit.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Use a private completed storage audit")
    report = json.loads((storage_audit / "audit.private.json").read_text())
    if report["compiled"] != 14 or report["total"] != 14:
        raise ValueError("Complete the real public storage compilation first")
    linked = json.loads((storage_audit / "linked.private.json").read_text())
    archive = storage_audit / "libPublicStorage.a"
    if (
        linked["link_exit"] != 0
        or hashlib.sha256(archive.read_bytes()).hexdigest() != linked["archive_sha256"]
    ):
        raise ValueError("The source storage archive was not link-qualified")
    # This creates fresh pattern/nonce inputs with the existing boundary checks.
    # Its raw-probe archive is retained, but only libBridgePciMedia is linked for
    # this new mode; the early raw-probe shutdown must never be linked with it.
    pattern = build(source, output, nonce)
    objects = []
    hashes = {}
    for name, recipe in (
        ("BridgePciMediaDevice.c", "osfmk/DEVELOPMENT/prng_random.o.json"),
        ("BridgePciMedia.cpp", "iokit/DEVELOPMENT/IOService.cpo.json"),
    ):
        guest = ROOT / "tools/native_xnu_probe" / name
        record_path = source / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837" / recipe
        record = json.loads(record_path.read_text().strip().rstrip(","))
        arguments = record["arguments"]
        destination = output / (guest.stem + ".o")
        command = []
        index = 0
        while index < len(arguments):
            value = arguments[index]
            if value == record["file"]:
                command.append(str(guest))
            elif value == "-o":
                command.extend(["-o", str(destination)])
                index += 1
            elif value == "-Xclang" and arguments[index + 1].startswith("-W"):
                index += 1
            elif not value.startswith("-W"):
                command.append(value)
            index += 1
        command.extend(
            [
                "-I",
                str(output),
                "-I",
                str(storage_audit / "include"),
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-Wno-unused-const-variable",
                "-Wno-error=missing-include-dirs",
                "-Wno-c99-designator",
                "-Wno-reorder-init-list",
                "-Wno-deprecated-enum-enum-conversion",
                "-Wno-unnecessary-virtual-specifier",
            ]
        )
        (output / (guest.stem + ".command.private.json")).write_text(json.dumps(command, indent=2))
        result = subprocess.run(
            command, cwd=record["directory"], capture_output=True, text=True, timeout=90
        )
        (output / (guest.stem + ".private.log")).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(
                "Guest adapter compilation failed; inspect " + guest.stem + ".private.log"
            )
        if struct.unpack_from("<4I", destination.read_bytes()) != (0xFEEDFACF, 0x100000C, 0, 1):
            raise ValueError("Wrong guest object ABI")
        objects.append(destination)
        hashes[name] = hashlib.sha256(guest.read_bytes()).hexdigest()
    media_archive = output / "libBridgePciMedia.a"
    subprocess.run(["xcrun", "ar", "rcs", str(media_archive), *map(str, objects)], check=True)
    result = {
        "nonce": nonce,
        "media_archive_sha256": hashlib.sha256(media_archive.read_bytes()).hexdigest(),
        "storage_archive_sha256": linked["archive_sha256"],
        "sources": hashes,
        "disk_sha256": pattern["disk_sha256"],
        "vm_started": False,
        "media_publication_verified": False,
        "ios_boot_verified": False,
    }
    (output / "media-probe.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--storage-audit", required=True, type=Path)
    parser.add_argument("--nonce", required=True)
    args = parser.parse_args()
    print(
        json.dumps(
            compile_adapter(args.source, args.output, args.storage_audit, args.nonce), indent=2
        )
    )
