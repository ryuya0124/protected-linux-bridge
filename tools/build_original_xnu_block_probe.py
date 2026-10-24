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

CLASS = "BridgeOriginalPciBlock"
MANGLED = str(len(CLASS)) + CLASS


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
    source: Path, output: Path, audit_path: Path, candidate: Path, nonce: str
) -> dict:
    source = source.resolve(strict=True)
    audit_path = audit_path.resolve(strict=True)
    candidate = candidate.resolve(strict=True)
    private = (ROOT / "artifacts").resolve()
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
        "disk_fnv64": pattern["disk_fnv64"],
        "virtual_methods_checked": 190,
        "concrete_overrides": overrides,
        "bindings": {name: hex(value) for name, value in sorted(bindings.items())},
        "original_code_modified": False,
        "live_class_size_checked": False,
        "vm_started": False,
        "original_bsd_readback_verified": False,
        "ios_boot_verified": False,
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
    args = parser.parse_args()
    print(
        json.dumps(
            compile_probe(args.source, args.output, args.audit, args.candidate, args.nonce),
            indent=2,
        )
    )
