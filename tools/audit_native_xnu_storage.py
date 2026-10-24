#!/usr/bin/env python3
"""Compile pinned public storage sources against XNU; never link, deploy or start a VM."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
STORAGE_REVISION = "e45fbe30a7879e0b16e26dddb78538f1b00e0bff"
BUILD_REVISION = "1a01c2afa197d975d04da892325c56b6b14aaecb"
COMPONENTS = (
    "IOStorage",
    "IOMedia",
    "IOBlockStorageDevice",
    "IOBlockStorageDriver",
    "IOMediaBSDClient",
    "IOPartitionScheme",
    "IOGUIDPartitionScheme",
    "IOFDiskPartitionScheme",
    "IOApplePartitionScheme",
    "IOFilterScheme",
    "IOUserBlockStorageDevice_kext",
    "IORequest",
    "IORequestsPool",
)


def require_revision(path: Path, revision: str) -> None:
    actual = subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != revision:
        raise ValueError("Unexpected source revision: " + str(path))
    # This audit must not quietly compile a patched or generated replacement.
    changed = subprocess.check_output(
        ["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"], text=True
    )
    if changed and revision == STORAGE_REVISION:
        raise ValueError("Use the unmodified public IOStorageFamily source")


def compile_command(record: dict, source: Path, destination: Path, include: Path) -> list[str]:
    arguments = record["arguments"]
    if arguments.count(record["file"]) != 1 or arguments.count("-o") != 1:
        raise ValueError("Ambiguous XNU compiler record")
    command = []
    index = 0
    while index < len(arguments):
        value = arguments[index]
        if value == record["file"]:
            command.append(str(source))
        elif value == "-o":
            command.extend(["-o", str(destination)])
            index += 1
        else:
            command.append(value)
        index += 1
    # The public source retains two legacy `register` declarations. This allows
    # that spelling with XNU's C++23 compiler without changing storage code.
    command.extend(["-Wno-register", "-I", str(include), "-I", str(include.parent / "generated")])
    return command


def generate_driverkit(build_source: Path, storage_source: Path, output: Path, sdk: Path) -> Path:
    generated = output / "generated"
    generated.mkdir()
    (generated / "BlockStorageDeviceDriverKit").mkdir()
    work = build_source / ".cache/xnu-work"
    implementation = generated / "IOUserBlockStorageDevice.iig.cpp"
    command = [
        "xcrun",
        "iig",
        "--def",
        str(storage_source / "BlockStorageDeviceDriverKit/IOUserBlockStorageDevice.iig"),
        "--header",
        str(generated / "BlockStorageDeviceDriverKit/IOUserBlockStorageDevice.h"),
        "--impl",
        str(implementation),
        "--framework-name",
        "BlockStorageDeviceDriverKit",
        "--",
        "-x",
        "c++",
        "-std=gnu++2b",
        "-D__IIG=1",
        "-DDRIVERKIT_PRIVATE=1",
        "-DPRIVATE_WIFI_ONLY=1",
        "-isysroot",
        str(sdk),
    ]
    for path in (
        work / "iokit",
        work / "osfmk",
        work / "bsd",
        work / "EXTERNAL_HEADERS",
        build_source / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/bsd",
    ):
        command.extend(["-I", str(path)])
    (output / "iig.command.private.json").write_text(json.dumps(command, indent=2) + "\n")
    process = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (output / "iig.private.log").write_text(process.stdout + process.stderr)
    process.check_returncode()
    return implementation


def symbol_closure(kernel: Path, objects: list[Path]) -> dict:
    """Static import availability is weaker than a link, ABI audit or device boot."""

    def symbols(path: Path, flags: list[str]) -> set[str]:
        text = subprocess.check_output(["xcrun", "nm", *flags, "-j", str(path)], text=True)
        return {line.strip() for line in text.splitlines() if line.strip()}

    exported = symbols(kernel, ["-g", "-U"])
    kernel_defined = symbols(kernel, ["-U"])
    defined = set(exported)
    undefined = set()
    for path in objects:
        defined.update(symbols(path, ["-g", "-U"]))
        undefined.update(symbols(path, ["-u"]))
    return {
        "kernel_sha256": hashlib.sha256(kernel.read_bytes()).hexdigest(),
        "undefined_before_resolution": len(undefined),
        "not_exported_but_defined_in_kernel": sorted((undefined - defined) & kernel_defined),
        "unresolved_after_kernel_and_objects": sorted(undefined - defined - kernel_defined),
        "link_performed": False,
        "abi_qualified": False,
    }


def audit(build_source: Path, storage_source: Path, output: Path) -> dict:
    build_source = build_source.resolve(strict=True)
    storage_source = storage_source.resolve(strict=True)
    output = output.resolve()
    private = (ROOT / "artifacts").resolve()
    if any(not path.is_relative_to(private) for path in (build_source, storage_source, output)):
        raise ValueError("Use ignored private artifact directories")
    if output.exists():
        raise ValueError("Do not overwrite an earlier audit")
    require_revision(build_source, BUILD_REVISION)
    require_revision(storage_source, STORAGE_REVISION)
    recipe = (
        build_source
        / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOService.cpo.json"
    )
    record = json.loads(recipe.read_text().strip().rstrip(","))
    output.mkdir(parents=True)
    include = output / "include"
    headers = include / "IOKit/storage"
    headers.mkdir(parents=True)
    for header in storage_source.glob("*.h"):
        shutil.copyfile(header, headers / header.name)
    sdk = Path(
        subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
    )
    shutil.copyfile(sdk / "usr/include/TargetConditionals.h", include / "TargetConditionals.h")
    generated = generate_driverkit(build_source, storage_source, output, sdk)
    results = []
    sources = [storage_source / (name + ".cpp") for name in COMPONENTS] + [generated]
    for source in sources:
        if shutil.disk_usage(output).free < 8 * 1024**3:
            raise RuntimeError("Insufficient Mac space; no more compiles started")
        component = source.stem
        command = compile_command(record, source, output / (component + ".o"), include)
        command.extend(
            [
                "-I",
                str(storage_source),
                "-I",
                str(output / "generated/BlockStorageDeviceDriverKit"),
            ]
        )
        (output / (component + ".command.private.json")).write_text(json.dumps(command, indent=2))
        process = subprocess.run(
            command, cwd=record["directory"], capture_output=True, text=True, timeout=90
        )
        log = process.stdout + process.stderr
        (output / (component + ".private.log")).write_text(log)
        errors = [line for line in log.splitlines() if "error:" in line]
        results.append(
            {
                "component": component,
                "exit_code": process.returncode,
                "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "errors": errors,
            }
        )
        print(component, "compiled" if process.returncode == 0 else "blocked", flush=True)
    report = {
        "source_revision": STORAGE_REVISION,
        "build_revision": BUILD_REVISION,
        "compiler_record_sha256": hashlib.sha256(recipe.read_bytes()).hexdigest(),
        "components": results,
        "compiled": sum(row["exit_code"] == 0 for row in results),
        "total": len(results),
        "driverkit_generated_from_public_iig": True,
        "linked": False,
        "storage_service_registered": False,
        "filesystem_mount_verified": False,
        "vm_started": False,
        "ios_boot_verified": False,
    }
    if report["compiled"] == report["total"]:
        kernel = (
            build_source
            / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf"
        )
        report["symbol_closure"] = symbol_closure(
            kernel, [output / (source.stem + ".o") for source in sources]
        )
    (output / "audit.private.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-source", required=True, type=Path)
    parser.add_argument("--storage-source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.build_source, args.storage_source, args.output), indent=2))
