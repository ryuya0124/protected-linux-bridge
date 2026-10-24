#!/usr/bin/env python3
"""Build a serial-only XNU ADT from one completed native UEFI trial; never launch a VM."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import runpy
import struct
import subprocess
import sys

from native_uefi_result import assess

ROOT = Path(__file__).resolve().parents[1]
DEVICE_REVISION = "512350a37a80f6b368e889aee22fafbcf79a28a4"


def cpu_metadata(console: str, nonce: str) -> tuple[int, int]:
    rows = re.findall(r"PLB_NATIVE_UEFI_CPU nonce=([0-9a-f]{32}) cntfrq=(\d+) mpidr=(\d+)", console)
    if len(rows) != 1 or rows[0][0] != nonce:
        raise ValueError("Missing, duplicate or mismatched guest CPU receipt")
    frequency, mpidr = map(int, rows[0][1:])
    if not 0 < frequency < 2**32 or not 0 <= mpidr < 2**64:
        raise ValueError("Guest CPU metadata is out of range")
    return frequency, mpidr & 0xFF00FFFFFF


def prepare(source: Path, probe: Path, output: Path, *, pci_probe: bool = False) -> dict:
    source = source.resolve(strict=True)
    probe = probe.resolve(strict=True)
    output = output.resolve()
    artifacts = (ROOT / "artifacts").resolve()
    if any(not path.is_relative_to(artifacts) for path in (source, probe, output)):
        raise ValueError("Only private developer artifacts paths are accepted")
    if output.exists() or output.with_suffix(output.suffix + ".json").exists():
        raise ValueError("Do not overwrite a previous guest artifact or receipt")
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    script = "scripts/make-droidvm-adt.py"
    original = subprocess.check_output(["git", "-C", str(source), "show", "HEAD:" + script])
    if revision != DEVICE_REVISION or (source / script).read_bytes() != original:
        raise ValueError("Unreviewed upstream ADT generator")
    contract = json.loads((probe / "probe.json").read_text())
    console = (probe / "console.log.private").read_text()
    assess(console, (probe / "runner.private.log").read_text(), contract)
    frequency, affinity = cpu_metadata(console, contract["nonce"])
    upstream = runpy.run_path(str(source / script))
    fdt = upstream["parse_fdt"]((probe / "guest-generated.dtb").read_bytes())
    vm = upstream["describe"](fdt)
    if (vm["dram_base"], vm["dram_size"], vm["cpu_regs"]) != (0x80000000, 0x20000000, [affinity]):
        raise ValueError("Expected the qualified single-CPU 512 MiB VM layout")
    dma = [p for p in fdt.values() if p.get("compatible") == b"restricted-dma-pool\0"]
    if len(dma) != 1 or dma[0].get("reg") != struct.pack(">QQ", 0x9E000000, 0x2000000):
        raise ValueError("The private/shared guest RAM boundary changed")
    uart_path = fdt["/chosen"]["stdout-path"].rstrip(b"\0").decode("ascii")
    uart = fdt[uart_path]
    if uart.get("compatible") != b"ns16550a\0" or uart.get("reg") != struct.pack(">QQ", 0x3F8, 8):
        raise ValueError("The qualified byte-wide virtual UART changed")
    pci_node = b""
    if pci_probe:
        pci = fdt.get("/pci", {})
        expected_ranges = struct.pack(
            ">14I",
            0x02000000,
            0,
            0x02000000,
            0,
            0x02000000,
            0,
            0x02000000,
            0x43000000,
            0,
            0xE0000000,
            0,
            0xE0000000,
            0xFF,
            0x20000000,
        )
        if (
            pci.get("compatible") != b"pci-host-cam-generic\0"
            or pci.get("reg") != struct.pack(">QQ", 0x10000, 0x1000000)
            or pci.get("bus-range") != bytes(8)
            or pci.get("ranges") != expected_ranges
        ):
            raise ValueError("The qualified PCI CAM/aperture layout changed")
        pci_node = upstream["node"](
            "bridge-pci",
            [
                ("reg", struct.pack("<QQ", 0x10000, 0x10000)),
                (
                    "pci-apertures",
                    struct.pack("<4Q", 0x02000000, 0x02000000, 0xE0000000, 0xFF20000000),
                ),
                ("shared-probe", struct.pack("<QQ", 0x9FFF0000, 8192)),
            ],
        )
    # The 32 MiB shared DMA window must never become executable/general XNU RAM.
    # Keep the kernel's owned allocation at 256 MiB; do not reclaim firmware RAM.
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            sys.executable,
            str(source / script),
            "--output",
            str(output),
            "--fdt",
            str(probe / "guest-generated.dtb"),
            "--dram-size",
            "0x1e000000",
            "--owned-memory-size",
            "0x10000000",
            "--timebase",
            str(frequency),
        ],
        check=True,
    )
    tree = bytearray(output.read_bytes())
    properties, children = struct.unpack_from("<II", tree)
    if properties == 0 or children == 0:
        raise ValueError("Empty generated Apple-format device tree")
    struct.pack_into("<I", tree, 4, children + 1 + bool(pci_probe))
    tree += upstream["node"](
        "bridge-uart", [("compatible", b"ns16550a\0"), ("reg", struct.pack("<QQ", 0x3F8, 8))]
    )
    tree += pci_node
    output.write_bytes(tree)
    report = {
        "adt_sha256": hashlib.sha256(tree).hexdigest(),
        "upstream_revision": revision,
        "uart_base": 0x3F8,
        "counter_hz": frequency,
        "boot_affinity": affinity,
        "private_ram_bytes": 480 * 1024**2,
        "kernel_window_bytes": 256 * 1024**2,
        "shared_ram_excluded": True,
        "pci_readback_probe": pci_probe,
        "device_boot_verified": False,
        "ios_boot_verified": False,
    }
    output.with_suffix(output.suffix + ".json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-source", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pci-probe", action="store_true")
    args = parser.parse_args()
    print(
        json.dumps(
            prepare(args.device_source, args.probe, args.output, pci_probe=args.pci_probe), indent=2
        )
    )
