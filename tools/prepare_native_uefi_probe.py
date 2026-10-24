#!/usr/bin/env python3
"""Prepare a pinned guest-only UEFI probe and synthetic read-only disk; never runs a VM."""

import argparse
import hashlib
import json
from pathlib import Path
import secrets
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_COMMIT = "90c4d52c7d11485318bbb4fb9291a15ea1550ab2"
EDK2_COMMIT = "b7a715f7c03c45c6b4575bf88596bfd79658b8ce"


def prepare(source: Path, output: Path) -> None:
    source = source.resolve(strict=True)
    output = output.resolve()
    artifacts = (ROOT / "artifacts").resolve()
    if not source.is_relative_to(artifacts) or not output.is_relative_to(artifacts):
        raise ValueError("Only private developer artifacts directories are allowed")
    if output.exists():
        raise ValueError("Use a new output directory; existing results are retained")
    commit = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    dirty = subprocess.check_output(
        ["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True
    )
    if commit != UPSTREAM_COMMIT or dirty:
        raise ValueError("Expected the clean, reviewed edk2-gunyah revision")
    dependency = subprocess.check_output(
        ["git", "-C", str(source / "edk2"), "rev-parse", "HEAD"], text=True
    ).strip()
    if dependency != EDK2_COMMIT:
        raise ValueError("Unexpected EDK2 dependency revision")
    dependency_state = subprocess.check_output(
        ["git", "-C", str(source / "edk2"), "submodule", "status"], text=True
    )
    if any(line and line[0] != " " for line in dependency_state.splitlines()):
        raise ValueError("Initialize EDK2 first-level dependencies at their recorded commits")
    build_script = source / "build.sh"
    build_text = build_script.read_text()
    fetch_command = "git submodule update --init --recursive --depth 1"
    if build_text.count(fetch_command) != 1:
        raise ValueError("Unrecognized firmware build script")
    library = source / "GunyahPkg/Library/PlatformBootManagerLibLight"
    boot = library / "PlatformBm.c"
    info = library / "PlatformBootManagerLib.inf"
    boot_text = boot.read_text()
    anchor = "  EfiBootManagerRefreshAllBootOption ();"
    if boot_text.count(anchor) != 1:
        raise ValueError("Unrecognized boot-manager source")
    declaration = "VOID\nEFIAPI\nPlatformBootManagerAfterConsole ("
    if boot_text.count(declaration) != 1:
        raise ValueError("Unrecognized boot-manager entry point")
    info_text = info.read_text()
    if "  QemuKernel.c" not in info_text or "[LibraryClasses]" not in info_text:
        raise ValueError("Unrecognized boot-manager INF")
    nonce = secrets.token_hex(16)
    seed = bytes.fromhex(nonce)
    disk = bytes((at * 17 + (at >> 9) * 29 + seed[at % 16]) & 255 for at in range(2 * 1024 * 1024))
    digest = 14695981039346656037
    for value in disk:
        digest = ((digest ^ value) * 1099511628211) & ((1 << 64) - 1)
    output.mkdir(parents=True)
    # Dependencies are initialized explicitly at their recorded first-level
    # pins. Avoid recursive OpenSSL test/development trees during the build.
    build_script.write_text(
        build_text.replace(
            fetch_command, "# Dependency fetching is deliberately separate from this build."
        )
    )
    (output / "synthetic.raw").write_bytes(disk)
    shutil.copyfile(ROOT / "tools/native_uefi_probe/BridgeProbe.c", library / "BridgeProbe.c")
    (library / "probe_contract.generated.h").write_text(
        '#define PLB_UEFI_NONCE "'
        + nonce
        + '"\n'
        + "static const UINT8 plb_probe_seed[16] = {"
        + ", ".join(str(value) for value in seed)
        + "};\n"
    )
    boot.write_text(
        boot_text.replace(
            anchor, "  EfiBootManagerConnectAll ();\n  PlbNativeProbe ();\n" + anchor
        ).replace(
            declaration,
            "VOID PlbNativeProbe (VOID);\n\nVOID\nEFIAPI\nPlatformBootManagerAfterConsole (",
            1,
        )
    )
    info_text = info_text.replace("  QemuKernel.c", "  QemuKernel.c\n  BridgeProbe.c")
    info_text = info_text.replace("[Packages]", "[Packages]\n  EmbeddedPkg/EmbeddedPkg.dec")
    info_text = info_text.replace("[LibraryClasses]", "[LibraryClasses]\n  SerialPortLib")
    # Keep all existing protocol declarations and add only missing ones.
    missing = [
        name
        for name in ("gEfiBlockIoProtocolGuid", "gFdtClientProtocolGuid")
        if name not in info_text
    ]
    if missing:
        info_text += "\n[Protocols]\n" + "".join("  " + name + "\n" for name in missing)
    info.write_text(info_text)
    record = {
        "upstream_commit": commit,
        "edk2_commit": EDK2_COMMIT,
        "nonce": nonce,
        "ram_mib": 512,
        "swiotlb_mib": 32,
        "vcpus": 1,
        "disk_bytes": len(disk),
        "disk_sha256": hashlib.sha256(disk).hexdigest(),
        "disk_fnv64": digest,
        "probe_source_sha256": hashlib.sha256((library / "BridgeProbe.c").read_bytes()).hexdigest(),
        "cpu_emulation": False,
        "guest_only": True,
        "vm_started": False,
        "ios_boot_claimed": False,
    }
    (output / "probe.json").write_text(json.dumps(record, indent=2) + "\n")
    print("NATIVE_UEFI_PROBE_PREPARED vm_started=0")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    prepare(arguments.source, arguments.output)
