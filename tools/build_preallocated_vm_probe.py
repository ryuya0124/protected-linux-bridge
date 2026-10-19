#!/usr/bin/env python3
"""Compile the pinned diskless runner. This script never installs or starts a VM."""

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
    parser.add_argument("--vmm", type=Path, required=True)
    parser.add_argument("--build-manifest", type=Path, required=True)
    parser.add_argument("--full-content", action="store_true")
    args = parser.parse_args()
    if shutil.disk_usage(ROOT).free < 21 * 1024**3:
        parser.error("Retain 20 GiB storage headroom")
    digest = hashlib.sha256(args.vmm.read_bytes()).hexdigest()
    vmm_build = json.loads(args.build_manifest.read_text())
    if (
        vmm_build.get("vmm_sha256") != digest
        or vmm_build.get("source_revision") != "8a4abfbaea5567de24e49cc43874c820098987d0"
        or set(vmm_build.get("features", []))
        not in (
            {"gunyah", "plb-preallocated-ram"},
            {"gunyah", "plb-preallocated-ram", "plb-split-ram"},
        )
    ):
        parser.error("VMM does not match the explicit minimal diagnostic build manifest")
    output = ROOT / "artifacts/preallocated-ram-handoff/vmm-build"
    if args.full_content:
        output = output / "full-content"
    output.mkdir(parents=True, exist_ok=True)
    pins = {
        "VMM": digest,
        "KERNEL": "95fe64403ffd031ce1ba8965098eed3073ae6db38be017801d1d77117f966dbc",
        "INITRAMFS": "3fcac6bca1659b39918abaf0e99e75b8a63ae1cd5d6c83417dd11713aa890219",
    }
    if args.full_content:
        pins["INITRAMFS"] = hashlib.sha256(
            (ROOT / "artifacts/memory-full-probe.cpio").read_bytes()
        ).hexdigest()
    header = output / "preallocated_vm.generated.h"
    header.write_text(
        "/* Generated identity pins for the separate developer artifact. */\n"
        + "\n".join(
            f'#define PLB_PREALLOCATED_{name}_SHA256 "{value}"' for name, value in pins.items()
        )
        + "\n"
    )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    binary = output / (
        "preallocated-vm-full-probe" if args.full_content else "preallocated-vm-probe"
    )
    subprocess.run(
        [
            str(compiler),
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
        ]
        + (["-DPLB_PREALLOCATED_FULL_CONTENT=1"] if args.full_content else [])
        + [
            "-I",
            str(output),
            str(ROOT / "tools/preallocated_vm_probe.c"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    manifest = {
        "purpose": "fixed private VMM, diskless protected Linux guest, adopted ordinary RAM",
        "vm_started_by_build": False,
        "pins": pins,
        "runner_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "source_revision": vmm_build["source_revision"],
        "vmm_features": vmm_build["features"],
        "readback_kind": "all_64_bit_words" if args.full_content else "page_markers",
    }
    sources = [
        "tools/preallocated_vm_probe.c",
        "tools/preallocated_vm_limits.h",
        "tools/preallocated_pin.h",
        "tools/preallocated_collapse.h",
        "tools/preallocated_ready.h",
        "tools/owned_page_summary.h",
        "tools/diagnostic_owner_lock.h",
        "native/profile_lock.h",
        "tools/host_memory_health.h",
    ]
    manifest["source_sha256"] = {
        name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources
    }
    (output / "probe-build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(binary)


if __name__ == "__main__":
    main()
