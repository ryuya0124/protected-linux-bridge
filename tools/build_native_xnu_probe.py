#!/usr/bin/env python3
"""Compile the guest PCI readback archive with the actual XNU ABI; never launches a VM."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REVISION = "1a01c2afa197d975d04da892325c56b6b14aaecb"


def build(source: Path, output: Path, nonce: str) -> dict:
    source = source.resolve(strict=True)
    output = output.resolve()
    private = (ROOT / "artifacts").resolve()
    if not re.fullmatch(r"[0-9a-f]{32}", nonce):
        raise ValueError("Invalid trial nonce")
    if not source.is_relative_to(private) or not output.is_relative_to(private) or output.exists():
        raise ValueError("Use a new private artifact directory")
    actual = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != REVISION:
        raise ValueError("Unexpected XNU build-driver revision")
    recipe = (
        source / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/prng_random.o.json"
    )
    record = json.loads(recipe.read_text().strip().rstrip(","))
    output.mkdir(parents=True)
    seed = bytes.fromhex(nonce)
    disk = bytes((at * 17 + (at >> 9) * 29 + seed[at % 16]) & 255 for at in range(2097152))
    digest = 14695981039346656037
    for byte in disk:
        digest = ((digest ^ byte) * 1099511628211) & (2**64 - 1)
    (output / "pattern.raw").write_bytes(disk)
    (output / "pci_contract.generated.h").write_text(
        '#define PLB_PCI_NONCE "'
        + nonce
        + '"\nstatic const uint8_t plb_pci_seed[16] = {'
        + ", ".join(map(str, seed))
        + "};\n"
    )
    guest_source = ROOT / "tools/native_xnu_probe/BridgePciBlock.c"
    destination = output / "BridgePciBlock.o"
    arguments = record["arguments"]
    command = []
    index = 0
    while index < len(arguments):
        value = arguments[index]
        if value == record["file"]:
            command.append(str(guest_source))
        elif value == "-o":
            command.extend(["-o", str(destination)])
            index += 1
        else:
            command.append(value)
        index += 1
    command += ["-I", str(output), "-Wall", "-Wextra", "-Werror", "-Wno-error=missing-include-dirs"]
    (output / "compile-command.private.json").write_text(json.dumps(command, indent=2) + "\n")
    result = subprocess.run(command, cwd=record["directory"], capture_output=True, text=True)
    (output / "compile.private.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError("Guest compilation failed; inspect the private compile log")
    data = destination.read_bytes()
    if struct.unpack_from("<4I", data) != (0xFEEDFACF, 0x100000C, 0, 1):
        raise ValueError("Expected an ordinary ARM64 guest object")
    archive = output / "libBridgePciProbe.a"
    subprocess.run(["xcrun", "ar", "rcs", str(archive), str(destination)], check=True)
    report = {
        "nonce": nonce,
        "disk_bytes": len(disk),
        "disk_fnv64": digest,
        "disk_sha256": hashlib.sha256(disk).hexdigest(),
        "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "source_sha256": hashlib.sha256(guest_source.read_bytes()).hexdigest(),
        "transport_header_sha256": hashlib.sha256(
            guest_source.with_name("virtio_pci_block.h").read_bytes()
        ).hexdigest(),
        "mapping_header_sha256": hashlib.sha256(
            guest_source.with_name("xnu_pci_support.h").read_bytes()
        ).hexdigest(),
        "vm_started": False,
        "ios_boot_verified": False,
    }
    (output / "pci-probe.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--nonce", required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.source, args.output, args.nonce), indent=2))
