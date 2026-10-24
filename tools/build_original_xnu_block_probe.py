#!/usr/bin/env python3
"""Compile the guest-only original-storage PCI fixture; never deploy or start a VM.

Addresses bind real, audited functions/data in a pinned Apple guest kernelcache.
No storage function is replaced with a success stub. Static ABI qualification
does not claim live metadata qualification; the guest checks its base size first.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

from build_native_xnu_probe import ROOT, build
from audit_native_xnu_storage import BUILD_REVISION, STORAGE_REVISION, require_revision
from native_ios_root_contract import validate as validate_root
from build_native_ios_block_policy import validate_policy, TABLE_BYTES

CLASS = "BridgeOriginalPciBlock"
MANGLED = str(len(CLASS)) + CLASS
APPOS_SHA256 = "52e568e1eaaede5fde4684ad10afc60f8fb236bfb5a23e0869bb0ed8a1d275d7"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def qualify_dispatch(audit: dict, compiled: list[str], readable: dict[str, str]) -> int:
    if (
        audit.get("public_source_revision") != STORAGE_REVISION
        or audit.get("method_slots") != 190
        or audit.get("kernel_bindings") != 176
        or audit.get("pure_virtual_slots") != 14
        or audit.get("compiled_public_class_size") != 0x90
        or audit.get("original_executable_code_unchanged") is not True
        or audit.get("abstract_complete_destructors_trap") is not True
    ):
        raise ValueError("Incomplete original storage ABI audit")
    rows = audit.get("methods", [])
    if len(rows) != 190 or len(compiled) != 190 or not all(compiled):
        raise ValueError("Incomplete concrete dispatch table")
    if "___cxa_pure_virtual" in compiled:
        raise ValueError("Concrete transport retains an abstract operation")
    overrides = 0
    for index, (row, actual) in enumerate(zip(rows, compiled, strict=True)):
        if row["slot"] != index:
            raise ValueError("Reordered audit slots")
        expected = row["reference_name"]
        own = actual.startswith(("__ZN" + MANGLED, "__ZNK" + MANGLED))
        if expected == "___cxa_pure_virtual":
            if not own:
                raise ValueError("Required concrete transport operation is absent")
        elif actual != expected:
            if not own or (
                index not in (0, 1)
                and readable[actual].split("::", 1)[1] != readable[expected].split("::", 1)[1]
            ):
                raise ValueError("Concrete dispatch mismatch at slot " + str(index))
        overrides += own
    return overrides


def select_bindings(audit: dict, undefined: list[str]) -> dict[str, int]:
    allowed = {
        row["reference_name"]: int(row["candidate_target"], 16)
        for row in audit["methods"]
        if not row["kernel_names"]
    }
    allowed.update(
        {
            "__ZN20IOBlockStorageDeviceC2EPK11OSMetaClass": int(
                audit["original_protected_constructor"], 16
            ),
            "__ZN20IOBlockStorageDeviceD2Ev": int(audit["original_base_destructor"], 16),
            "__ZN20IOBlockStorageDevice10gMetaClassE": int(audit["original_metaclass"], 16),
            "__ZTV20IOBlockStorageDevice": int(audit["vtable"], 16),
        }
    )
    selected = {}
    for name in undefined:
        if "IOBlockStorageDevice" not in name:
            continue
        if name.endswith(("IOBlockStorageDeviceD0Ev", "IOBlockStorageDeviceD1Ev")):
            raise ValueError("Abstract complete/deleting destructor would trap")
        if name not in allowed:
            raise ValueError("Unaudited original storage import: " + name)
        selected[name] = allowed[name]
    if not selected:
        raise ValueError("No genuine original storage imports")
    return selected


def object_table(parser, obj: bytes) -> tuple[list[tuple[str, int, int]], list[str]]:
    image = parser(obj)
    offset = next(at for cmd, at, _ in image["commands"] if cmd == 2)
    sy, count, st, _ = struct.unpack_from("<4I", obj, offset + 8)
    symbols = []
    for index in range(count):
        ix, kind, _, _, value = struct.unpack_from("<IBBHQ", obj, sy + index * 16)
        name = obj[st + ix : obj.index(b"\0", st + ix)].decode()
        symbols.append((name, value, kind))
    vtable = next(value for name, value, _ in symbols if name == "__ZTV" + MANGLED)
    relocations = {}
    for segment in image["segments"]:
        for section in segment["sections"]:
            if section["va"] <= vtable < section["va"] + section["size"]:
                ro, rc = struct.unpack_from("<II", obj, section["command_offset"] + 56)
                for index in range(rc):
                    address, word = struct.unpack_from("<II", obj, ro + index * 8)
                    if word >> 27 & 1 and word >> 28 == 0 and word >> 25 & 3 == 3:
                        relocations[section["va"] + address] = symbols[word & 0xFFFFFF][0]
    if vtable + 16 + 190 * 8 in relocations:
        raise ValueError("Unexpected additional virtual method")
    return symbols, [relocations.get(vtable + 16 + index * 8) for index in range(190)]


def compile_probe(
    source: Path,
    output: Path,
    audit_path: Path,
    candidate: Path,
    nonce: str,
    apfs_fixture: Path | None = None,
    mount_probe: bool = False,
    root_disk: Path | None = None,
    block_policy: Path | None = None,
    inject_block_fault: bool = False,
) -> dict:
    source = source.resolve(strict=True)
    audit_path = audit_path.resolve(strict=True)
    candidate = candidate.resolve(strict=True)
    private = (ROOT / "artifacts").resolve()
    if root_disk is not None and (apfs_fixture is not None or not mount_probe):
        raise ValueError("Main OS requires an exclusive, explicit mount diagnostic")
    if mount_probe and apfs_fixture is None and root_disk is None:
        raise ValueError("Mount diagnostic requires a separately pinned official fixture")
    root = validate_root(root_disk) if root_disk is not None else None
    if (block_policy is not None and root is None) or (inject_block_fault and block_policy is None):
        raise ValueError("Block verification/fault controls require the separate main-root policy")
    policy = validate_policy(block_policy) if block_policy is not None else None
    if any(not path.is_relative_to(private) for path in (source, audit_path, candidate)):
        raise ValueError("Use private, reviewed source and audit artifacts")
    require_revision(source, BUILD_REVISION)
    audit = json.loads(audit_path.read_text())
    if digest(candidate) != audit["candidate_sha256"]:
        raise ValueError("Audited candidate identity changed")
    public_source = source / "refer/IOStorageFamily"
    require_revision(public_source, STORAGE_REVISION)
    if digest(public_source / "IOBlockStorageDevice.cpp") != audit["source_sha256"]:
        raise ValueError("Audited public implementation identity changed")
    # Existing fresh-directory and nonce checks also create the immutable fixture.
    pattern = build(source, output, nonce)
    output = output.resolve()
    if apfs_fixture is not None:
        fixture = apfs_fixture.resolve(strict=True)
        if not fixture.is_relative_to(private) or fixture.stat().st_size != 10485760:
            raise ValueError("Use the private, pinned10MiB official AppOS fixture")
        data = fixture.read_bytes()
        if hashlib.sha256(data).hexdigest() != APPOS_SHA256 or data[32:36] != b"NXSB":
            raise ValueError("Wrong official APFS fixture")
        fnv = 14695981039346656037
        for byte in data:
            fnv = ((fnv ^ byte) * 1099511628211) & (2**64 - 1)
        (output / "pattern.raw").write_bytes(data)
        with (output / "pci_contract.generated.h").open("a") as header:
            header.write(
                f"#define PLB_PCI_DATA_BYTES {len(data)}\n"
                f"#define PLB_PCI_APFS_FIXTURE 1\n#define PLB_PCI_EXPECTED_FNV {fnv}ULL\n"
            )
            if mount_probe:
                header.write("#define PLB_PCI_APFS_MOUNT 1\n")
        pattern.update(
            disk_bytes=len(data),
            disk_sha256=APPOS_SHA256,
            disk_fnv64=fnv,
            fixture="official Cryptex1,AppOS",
            original_raw_probe_not_linked=True,
        )
        (output / "pci-probe.json").write_text(json.dumps(pattern, indent=2) + "\n")
    if root is not None:
        # Reference the verified existing Mac image; do not duplicate six GiB.
        # Deployment creates a distinct root-owned read-only guest input.
        (output / "pattern.raw").unlink()
        with (output / "pci_contract.generated.h").open("a") as header:
            header.write(
                f"#define PLB_PCI_DATA_BYTES {root['disk_bytes']}ULL\n"
                "#define PLB_PCI_MAIN_ROOT 1\n#define PLB_PCI_APFS_MOUNT 1\n"
                f"#define PLB_PCI_EXPECTED_PREFIX_FNV {root['prefix_fnv64']}ULL\n"
                f"#define PLB_PCI_EXPECTED_TAIL_FNV {root['tail_fnv64']}ULL\n"
            )
        pattern.update(root, original_raw_probe_not_linked=True)
        pattern.pop("disk_fnv64")
        (output / "pci-probe.json").write_text(json.dumps(pattern, indent=2) + "\n")
    if policy is not None:
        with (output / "pci_contract.generated.h").open("a") as header:
            header.write("#define PLB_PCI_BLOCK_POLICY 1\n")
            if inject_block_fault:
                header.write("#define PLB_PCI_BLOCK_FAULT 1\n")
        values = ", ".join(f"0x{byte:02x}" for byte in bytes.fromhex(policy["table_sha256"]))
        (output / "root_policy.generated.h").write_text(
            f"#define PLB_ROOT_TABLE_BYTES {TABLE_BYTES}ULL\n"
            f"#define PLB_ROOT_IMAGE_BYTES {root['disk_bytes']}ULL\n"
            f"#define PLB_ROOT_TABLE_SHA256_BYTES {{{values}}}\n"
        )
        pattern["block_policy"] = policy
        pattern["inject_block_fault"] = inject_block_fault
        (output / "pci-probe.json").write_text(json.dumps(pattern, indent=2) + "\n")
    includes = source / "build/storage-abi/include"
    header = includes / "IOKit/storage/IOBlockStorageDevice.h"
    if digest(header) != digest(public_source / "IOBlockStorageDevice.h"):
        raise ValueError("Public ABI header identity changed")
    objects = []
    for name, recipe in (
        ("BridgePciMediaDevice.c", "osfmk/DEVELOPMENT/prng_random.o.json"),
        (CLASS + ".cpp", "iokit/DEVELOPMENT/IOService.cpo.json"),
    ):
        guest = ROOT / "tools/native_xnu_probe" / name
        record = json.loads(
            (source / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837" / recipe)
            .read_text()
            .strip()
            .rstrip(",")
        )
        args = record["arguments"]
        destination = output / (guest.stem + ".o")
        command = []
        index = 0
        while index < len(args):
            value = args[index]
            if value == record["file"]:
                command.append(str(guest))
            elif value == "-o":
                command.extend(["-o", str(destination)])
                index += 1
            elif value in ("-MF", "-MJ"):
                index += 1
            elif value == "-Xclang" and args[index + 1].startswith("-W"):
                index += 1
            elif not value.startswith("-W") and value not in ("-MD", "-MP"):
                command.append(value)
            index += 1
        command.extend(
            [
                "-I",
                str(output),
                "-I",
                str(includes),
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
        compiled = subprocess.run(
            command, cwd=record["directory"], capture_output=True, text=True, timeout=90
        )
        (output / (guest.stem + ".private.log")).write_text(compiled.stdout + compiled.stderr)
        if compiled.returncode:
            raise RuntimeError("Compile failed; inspect " + guest.stem + ".private.log")
        if struct.unpack_from("<4I", destination.read_bytes()) != (0xFEEDFACF, 0x100000C, 0, 1):
            raise ValueError("Incorrect guest object ABI")
        objects.append(destination)
    if policy is not None:
        assembly = output / "block-policy.S"
        binary = block_policy.resolve(strict=True) / "block-hashes.bin"
        assembly.write_text(
            ".section __TEXT,__const\n.p2align 4\n.globl _plb_root_hashes\n"
            "_plb_root_hashes:\n.incbin " + json.dumps(str(binary)) + "\n"
            ".globl _plb_root_hashes_end\n_plb_root_hashes_end:\n"
        )
        policy_object = output / "block-policy.o"
        subprocess.run(
            [
                "xcrun",
                "clang",
                "-target",
                "arm64-apple-ios17.0",
                "-c",
                str(assembly),
                "-o",
                str(policy_object),
            ],
            check=True,
        )
        objects.append(policy_object)
    specification = importlib.util.spec_from_file_location(
        "pinned_native_prelink", source / "kits/NativePrelink/native_prelink.py"
    )
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    symbols, compiled = object_table(module.macho, objects[1].read_bytes())
    names = list(dict.fromkeys(compiled + [row["reference_name"] for row in audit["methods"]]))
    if None in names:
        raise ValueError("Missing vtable relocation")
    demangled = subprocess.check_output(
        ["xcrun", "c++filt"], input="\n".join(name[1:] for name in names) + "\n", text=True
    ).splitlines()
    overrides = qualify_dispatch(audit, compiled, dict(zip(names, demangled, strict=True)))
    bindings = select_bindings(audit, [name for name, _, kind in symbols if kind == 1])
    assembly = output / "original-bindings.S"
    assembly.write_text(
        "".join(
            f".globl {name}\n.set {name}, {value:#x}\n" for name, value in sorted(bindings.items())
        )
    )
    binding_object = output / "original-bindings.o"
    subprocess.run(
        [
            "xcrun",
            "clang",
            "-target",
            "arm64-apple-ios17.0",
            "-Werror",
            "-Wno-incompatible-sysroot",
            "-c",
            str(assembly),
            "-o",
            str(binding_object),
        ],
        check=True,
    )
    archive = output / ("lib" + CLASS + ".a")
    subprocess.run(
        ["xcrun", "ar", "rcs", str(archive), *map(str, objects), str(binding_object)], check=True
    )
    report = {
        "nonce": nonce,
        "archive_sha256": digest(archive),
        "candidate_sha256": audit["candidate_sha256"],
        "audit_sha256": digest(audit_path),
        "sources": {
            path.name: digest(path) for path in (ROOT / "tools/native_xnu_probe").glob("Bridge*.*")
        },
        "disk_sha256": pattern["disk_sha256"],
        "disk_bytes": pattern["disk_bytes"],
        "disk_fnv64": pattern.get("disk_fnv64"),
        "main_root_probe": root is not None,
        "block_policy": policy,
        "verification_headers": (
            {
                name: digest(ROOT / "tools/native_xnu_probe" / name)
                for name in ("verified_hash.h", "verified_read.h")
            }
            if policy is not None
            else None
        ),
        "inject_block_fault": inject_block_fault,
        "virtual_methods_checked": 190,
        "concrete_overrides": overrides,
        "bindings": {name: hex(value) for name, value in sorted(bindings.items())},
        "original_code_modified": False,
        "live_class_size_checked": False,
        "vm_started": False,
        "original_bsd_readback_verified": False,
        "ios_boot_verified": False,
        "mount_probe": mount_probe,
    }
    (output / "original-probe.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--audit", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--nonce", required=True)
    parser.add_argument("--apfs-fixture", type=Path, help="Only the exact pinned small AppOS image")
    parser.add_argument(
        "--root-disk", type=Path, help="Separate exact main-OS preparation directory"
    )
    parser.add_argument("--block-policy", type=Path)
    parser.add_argument("--inject-block-fault", action="store_true")
    parser.add_argument(
        "--mount-probe", action="store_true", help="Guest-only root mount/authentication diagnostic"
    )
    args = parser.parse_args()
    print(
        json.dumps(
            compile_probe(
                args.source,
                args.output,
                args.audit,
                args.candidate,
                args.nonce,
                args.apfs_fixture,
                args.mount_probe,
                args.root_disk,
                args.block_policy,
                args.inject_block_fault,
            ),
            indent=2,
        )
    )
