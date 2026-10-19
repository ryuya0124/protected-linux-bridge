#!/usr/bin/env python3
"""Inspect the pinned saved VMM on the build machine; never execute or patch it.

Extract MiniDebugInfo and disassemble three verified function ranges from the
original image. All address-bearing output stays under ignored artifacts/.
The summary contains names, sizes and hashes, not runtime/GPU success claims.
"""

import argparse
import hashlib
import json
import lzma
import os
from pathlib import Path
import stat
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = "9b4d56db70be1d3d646a08da0cb3e6eb95637457e5f15cb3b1c627f348e3e847"
MAX_IMAGE = 16 * 1024 * 1024
MAX_DEBUG = 16 * 1024 * 1024
HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
SECTION = struct.Struct("<IIQQQQIIQQ")
FUNCTIONS = {
    "gunyah-vm-new": "<hypervisor::gunyah::GunyahVm>::new",
    "guest-memory-new": "<vm_memory::guest_memory::GuestMemory>::new_with_options",
    "shared-memory-new": "<base::shm::SharedMemory as base::shm::PlatformSharedMemory>::new",
}


def slice_checked(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError("ELF range outside saved image")
    return data[offset : offset + size]


def sections(data):
    if len(data) < HEADER.size:
        raise ValueError("Truncated ELF header")
    header = HEADER.unpack_from(data)
    if header[0][:7] != b"\x7fELF\x02\x01\x01" or header[2] != 183:
        raise ValueError("Require little-endian AArch64 ELF64")
    if header[11] != SECTION.size or not 1 <= header[12] <= 512 or header[13] >= header[12]:
        raise ValueError("Unsupported ELF section table")
    table = slice_checked(data, header[6], header[12] * SECTION.size)
    entries = [SECTION.unpack_from(table, index * SECTION.size) for index in range(header[12])]
    names = entries[header[13]]
    strings = slice_checked(data, names[4], names[5])
    result = {}
    for entry in entries:
        if entry[0] >= len(strings):
            raise ValueError("ELF section name outside table")
        end = strings.find(b"\0", entry[0])
        if end < 0:
            raise ValueError("Unterminated ELF section name")
        name = strings[entry[0] : end].decode("ascii", errors="strict")
        if name in result:
            raise ValueError("Ambiguous ELF section name")
        result[name] = entry
    return result


def extract_debug(data):
    entry = sections(data).get(".gnu_debugdata")
    if entry is None:
        raise ValueError("Saved image lacks MiniDebugInfo")
    compressed = slice_checked(data, entry[4], entry[5])
    decoder = lzma.LZMADecompressor(memlimit=64 * 1024 * 1024)
    debug = decoder.decompress(compressed, max_length=MAX_DEBUG + 1)
    if len(debug) > MAX_DEBUG or not decoder.eof or decoder.unused_data:
        raise ValueError("Oversized, incomplete or trailing MiniDebugInfo")
    sections(debug)
    return debug


def select_functions(nm_text, original):
    text = sections(original).get(".text")
    if text is None:
        raise ValueError("Saved image lacks text")
    text_start, text_size = text[3], text[5]
    selected = {}
    for line in nm_text.splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) != 4 or fields[2].upper() != "T":
            continue
        address, size, _, name = fields
        for tag, expected in FUNCTIONS.items():
            if name != expected:
                continue
            start, length = int(address, 16), int(size, 16)
            if tag in selected or not 4 <= length <= 16384 or start % 4 or length % 4:
                raise ValueError("Ambiguous or invalid function range")
            if start < text_start or start + length > text_start + text_size:
                raise ValueError("Function is outside saved text")
            selected[tag] = (start, length, name)
    if set(selected) != set(FUNCTIONS):
        raise ValueError("Required function symbols not identified")
    return selected


def write_private(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)


def inspect(image, output, llvm):
    fd = os.open(image, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    with os.fdopen(fd, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_IMAGE:
            raise ValueError("Require a bounded regular saved image")
        data = stream.read(MAX_IMAGE + 1)
    if len(data) > MAX_IMAGE or hashlib.sha256(data).hexdigest() != PIN:
        raise ValueError("Saved VMM does not match the reviewed image")
    debug = extract_debug(data)
    destination = output.resolve()
    if not destination.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Private analysis output must stay under ignored artifacts")
    destination.mkdir(parents=True, mode=0o700, exist_ok=False)
    # Disassemble the bytes whose digest was checked, not a path that another
    # process could replace between the digest and the external tool invocation.
    snapshot = destination / "original.private.elf"
    write_private(snapshot, data)
    mini = destination / "minidebug.private.elf"
    write_private(mini, debug)
    symbols = subprocess.check_output(
        [str(llvm / "llvm-nm"), "--defined-only", "--demangle", "--print-size", str(mini)],
        timeout=30,
    )
    write_private(destination / "symbols.private.txt", symbols)
    selected = select_functions(symbols.decode("utf-8", errors="strict"), data)
    for tag, (start, size, _) in selected.items():
        assembly = subprocess.check_output(
            [
                str(llvm / "llvm-objdump"),
                "--disassemble",
                "--demangle",
                "--start-address=" + hex(start),
                "--stop-address=" + hex(start + size),
                str(snapshot),
            ],
            timeout=30,
        )
        write_private(destination / (tag + ".private.asm"), assembly)
    summary = {
        "image_sha256": PIN,
        "minidebug_bytes": len(debug),
        "minidebug_sha256": hashlib.sha256(debug).hexdigest(),
        "functions": {name: size for _, size, name in selected.values()},
        "image_executed": False,
        "image_patched": False,
        "device_accessed": False,
        "guest_memory_allocated": False,
        "runtime_handoff_validated": False,
        "interpretation": "Static function identification only; inspect private assembly manually",
    }
    write_private(destination / "summary.json", (json.dumps(summary, indent=2) + "\n").encode())
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--llvm-directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        summary = inspect(args.image, args.output, args.llvm_directory)
    except (OSError, ValueError, lzma.LZMAError, subprocess.SubprocessError):
        parser.exit(2, "Saved-image analysis refused; check pin, input and fresh private output\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
