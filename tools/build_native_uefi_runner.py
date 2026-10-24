#!/usr/bin/env python3
"""Compile one boot-bound Android diagnostic runner; does not connect or launch anything."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]


def build(
    output: Path,
    snapshot: Path,
    sdk: Path,
    *,
    xnu_serial: bool = False,
    pci_probe: bool = False,
    apfs_probe: bool = False,
) -> None:
    if pci_probe and not xnu_serial:
        raise ValueError("The PCI readback is a distinct XNU-only diagnostic")
    if apfs_probe and not (pci_probe and xnu_serial):
        raise ValueError("The small APFS fixture requires the distinct XNU PCI profile")
    output = output.resolve(strict=True)
    if not output.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Use a private artifacts directory")
    if (output / "runner").exists() or (output / "runner_contract.generated.h").exists():
        raise ValueError("Runner outputs already exist; do not overwrite trial pins")
    contract = json.loads((output / "probe.json").read_text())
    host = json.loads(snapshot.read_text())
    nonce = contract["nonce"]
    if not re.fullmatch(r"[0-9a-f]{32}", nonce):
        raise ValueError("Invalid nonce")
    boot_id = host["boot_id"]
    if str(uuid.UUID(boot_id)) != boot_id:
        raise ValueError("Invalid boot identity")
    cores = host["cores"]
    if len(cores) != 3 or len({item["pid"] for item in cores}) != 3:
        raise ValueError("Require zygote, system_server and trusted VM identities")
    for core in cores:
        if not isinstance(core["pid"], int) or not 1 < core["pid"] < 4194304:
            raise ValueError("Invalid core process identity")
        if not isinstance(core["start_ticks"], int) or not 0 < core["start_ticks"] < 2**63:
            raise ValueError("Invalid core process birth time")
    firmware = (output / "firmware.fd").read_bytes()
    if len(firmware) != 4 * 1024 * 1024 or firmware[56:60] != b"ARMd":
        raise ValueError("Expected the reviewed 4 MiB ARM64 kernel-header firmware")
    if firmware[8:16] != b"\0" * 8:
        raise ValueError("Expected the Gunyah zero text offset")
    disk = (output / "synthetic.raw").read_bytes()
    expected_size = (64 if xnu_serial else 2) * 1024 * 1024
    if len(disk) != expected_size or hashlib.sha256(disk).hexdigest() != contract["disk_sha256"]:
        raise ValueError("New diagnostic disk changed")
    if xnu_serial and (disk[54:62] != b"FAT16   " or disk[510:512] != b"\x55\xaa"):
        raise ValueError("Expected the separately prepared read-only XNU FAT16 payload")
    profile = "xnu" if xnu_serial else "uefi"
    base = f"/data/user/0/net.ryuya_dev.protectedlinux/files/native-{profile}-" + nonce[:12]
    firmware_hash = hashlib.sha256(firmware).hexdigest()
    header = (
        '#define PLB_UEFI_BASE "'
        + base
        + '"\n'
        + '#define PLB_UEFI_BOOT_ID "'
        + boot_id
        + '"\n'
        + '#define PLB_UEFI_NONCE "'
        + nonce
        + '"\n'
        + '#define PLB_UEFI_FIRMWARE_SHA256 "'
        + firmware_hash
        + '"\n'
        + '#define PLB_UEFI_DISK_SHA256 "'
        + contract["disk_sha256"]
        + '"\n'
        + "static const int plb_uefi_core_pids[3] = {"
        + ", ".join(str(core["pid"]) for core in cores)
        + "};\n"
        + "static const unsigned long long plb_uefi_core_starts[3] = {"
        + ", ".join(str(core["start_ticks"]) + "ULL" for core in cores)
        + "};\n"
    )
    if xnu_serial:
        header += '#define PLB_UEFI_VM_NAME "plb-native-xnu"\n'
        header += '#define PLB_UEFI_RESULT_KIND "XNU"\n'
    if pci_probe:
        pattern = (output / "pattern.raw").read_bytes()
        pci = json.loads((output / "pci-probe.json").read_text())
        expected_bytes = 10485760 if apfs_probe else 2097152
        if (
            pci["nonce"] != nonce
            or len(pattern) != expected_bytes
            or hashlib.sha256(pattern).hexdigest() != pci["disk_sha256"]
        ):
            raise ValueError("The independent synthetic PCI disk changed")
        if (
            apfs_probe
            and pci["disk_sha256"]
            != "52e568e1eaaede5fde4684ad10afc60f8fb236bfb5a23e0869bb0ed8a1d275d7"
        ):
            raise ValueError("Wrong pinned small APFS fixture")
        header += "#define PLB_UEFI_PCI_PROBE 1\n"
        header += '#define PLB_UEFI_PATTERN_SHA256 "' + pci["disk_sha256"] + '"\n'
    (output / "runner_contract.generated.h").write_text(header)
    compiler = (
        sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android35-clang"
    )
    subprocess.run(
        [
            str(compiler),
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(output),
            str(ROOT / "tools/native_uefi_probe/runner.c"),
            "-o",
            str(output / "runner"),
        ],
        check=True,
    )
    contract.update(
        {
            "firmware_sha256": firmware_hash,
            "device_directory": base,
            "runner_sha256": hashlib.sha256((output / "runner").read_bytes()).hexdigest(),
        }
    )
    (output / "probe.json").write_text(json.dumps(contract, indent=2) + "\n")
    print(f"NATIVE_{profile.upper()}_RUNNER_BUILT vm_started=0")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--host-snapshot", required=True, type=Path)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument(
        "--xnu-serial", action="store_true", help="Build the distinct XNU serial-only profile"
    )
    parser.add_argument("--pci-probe", action="store_true")
    parser.add_argument("--apfs-probe", action="store_true")
    args = parser.parse_args()
    build(
        args.output,
        args.host_snapshot,
        args.sdk,
        xnu_serial=args.xnu_serial,
        pci_probe=args.pci_probe,
        apfs_probe=args.apfs_probe,
    )
