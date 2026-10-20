#!/usr/bin/env python3
"""Inspect a pinned saved Android 17 VMM. Never load, execute or modify it.

MiniDebugInfo identifies real function bodies; it is not a dynamic export table.
Keep binary snapshots, addresses and disassembly in ignored artifacts only.
"""

import argparse
import hashlib
import json
import lzma
import os
from pathlib import Path
import re
import stat
import struct
import subprocess

from inspect_crosvm_minidebug import (
    HEADER,
    ROOT,
    SECTION,
    extract_debug,
    sections,
    slice_checked,
    write_private,
)

PIN = "a0cfa5b957d119202b85a069928b3b56bd080329efed42eb83b182c9513885e9"
MAX_IMAGE = 16 * 1024 * 1024
SYMBOL = struct.Struct("<IBBHQQ")
DYNAMIC = struct.Struct("<qQ")
FUNCTIONS = {
    "renderer-init": "stream_renderer_init",
    "renderer-export": "stream_renderer_export_blob",
    "display-create": "create_android_display_context",
    "display-flip": "android_display_flip_to",
    "display-import": "<gpu_display::gpu_display_android::DisplayAndroid as gpu_display::DisplayT>::import_resource",
    "display-surface-flip": "<gpu_display::gpu_display_android::AndroidSurface as gpu_display::GpuDisplaySurface>::flip_to",
    "display-buffer": "(anonymous namespace)::AndroidDisplaySurface::setBufferLocked(AHardwareBuffer*)",
}


def table_string(strings, offset):
    if offset < 0 or offset >= len(strings):
        raise ValueError("ELF string outside table")
    end = strings.find(b"\0", offset)
    if end < 0:
        raise ValueError("Unterminated ELF string")
    return strings[offset:end].decode("ascii", errors="strict")


def linkage(data):
    entries = sections(data)
    strings = entries.get(".dynstr")
    symbols = entries.get(".dynsym")
    dynamic = entries.get(".dynamic")
    if strings is None or symbols is None or dynamic is None:
        raise ValueError("Require dynamic ELF metadata")
    if strings[1] != 3 or symbols[1] != 11 or symbols[9] != SYMBOL.size or dynamic[1] != 6:
        raise ValueError("Unsupported dynamic symbol table")
    header = HEADER.unpack_from(data)
    for entry in (symbols, dynamic):
        if entry[6] >= header[12]:
            raise ValueError("Invalid dynamic string table link")
        linked = SECTION.unpack_from(data, header[6] + entry[6] * SECTION.size)
        if linked != strings:
            raise ValueError("Unexpected dynamic string table")
    names = slice_checked(data, strings[4], strings[5])
    symbol_bytes = slice_checked(data, symbols[4], symbols[5])
    dynamic_bytes = slice_checked(data, dynamic[4], dynamic[5])
    if (
        len(symbol_bytes) % SYMBOL.size
        or dynamic[9] != DYNAMIC.size
        or len(dynamic_bytes) % DYNAMIC.size
    ):
        raise ValueError("Truncated dynamic table")
    exports = []
    for offset in range(0, len(symbol_bytes), SYMBOL.size):
        name, info, other, section, _, _ = SYMBOL.unpack_from(symbol_bytes, offset)
        text = table_string(names, name)
        binding, symbol_type, visibility = info >> 4, info & 15, other & 3
        if section and binding in (1, 2) and symbol_type in (2, 10) and visibility in (0, 3):
            if text.startswith(("stream_renderer_", "gfxstream_backend_", "plb_crosvm_")):
                exports.append(text)
    needed, flags, terminated = [], [], False
    for offset in range(0, len(dynamic_bytes), DYNAMIC.size):
        tag, value = DYNAMIC.unpack_from(dynamic_bytes, offset)
        if tag == 0:
            terminated = True
            break
        if tag == 1:
            needed.append(table_string(names, value))
        if tag == 0x6FFFFFFB:
            flags.append(value)
    if not terminated or len(flags) > 1:
        raise ValueError("Ambiguous dynamic flags or missing terminator")
    return {
        "elf_type": header[1],
        "df_1_pie": bool(flags and flags[0] & 0x08000000),
        "needed_libraries": needed,
        "dynamic_gpu_exports": sorted(set(exports)),
    }


def select_functions(nm_text, data):
    text = sections(data)[".text"]
    result = {}
    for line in nm_text.splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) != 4 or fields[2] != "T":
            continue
        address, size, _, name = fields
        for tag, expected in FUNCTIONS.items():
            if name != expected:
                continue
            start, length = int(address, 16), int(size, 16)
            if tag in result or length < 4 or length > 65536 or start % 4 or length % 4:
                raise ValueError("Ambiguous GPU function")
            if start < text[3] or start + length > text[3] + text[5]:
                raise ValueError("GPU function outside saved text")
            result[tag] = (start, length, name)
    if set(result) != set(FUNCTIONS):
        raise ValueError("Required Android 17 GPU functions missing")
    return result


def inspect(image, output, llvm):
    fd = os.open(image, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    with os.fdopen(fd, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_IMAGE:
            raise ValueError("Require a bounded regular saved image")
        data = stream.read(MAX_IMAGE + 1)
    if len(data) > MAX_IMAGE or hashlib.sha256(data).hexdigest() != PIN:
        raise ValueError("Unrecognized Android 17 VMM")
    summary = linkage(data)
    debug = extract_debug(data)
    destination = output.resolve()
    if not destination.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Output must remain private in artifacts")
    destination.mkdir(parents=True, mode=0o700, exist_ok=False)
    snapshot = destination / "crosvm.private.elf"
    mini = destination / "minidebug.private.elf"
    write_private(snapshot, data)
    write_private(mini, debug)
    symbols = subprocess.check_output(
        [str(llvm / "llvm-nm"), "--defined-only", "--demangle", "--print-size", str(mini)],
        timeout=30,
    )
    write_private(destination / "symbols.private.txt", symbols)
    selected = select_functions(symbols.decode("utf-8", errors="strict"), data)
    summary["functions"] = {}
    for tag, (address, size, name) in selected.items():
        disassembly = subprocess.check_output(
            [
                str(llvm / "llvm-objdump"),
                "--disassemble",
                "--no-show-raw-insn",
                "--start-address=" + hex(address),
                "--stop-address=" + hex(address + size),
                str(snapshot),
            ],
            timeout=30,
        )
        write_private(destination / (tag + ".private.asm"), disassembly)
        calls = sorted(set(re.findall(r"<([^<>]+)@plt>", disassembly.decode())))
        summary["functions"][tag] = {"name": name, "bytes": size, "referenced_plt_symbols": calls}
    summary.update(
        {
            "schema": 1,
            "image_sha256": PIN,
            "image_bytes": len(data),
            "image_executed": False,
            "image_patched": False,
            "device_accessed": False,
            "gpu_rendering_validated": False,
            "display_connected": False,
        }
    )
    write_private(destination / "summary.json", (json.dumps(summary, indent=2) + "\n").encode())
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--llvm-directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = inspect(args.image, args.output, args.llvm_directory)
    except (OSError, ValueError, lzma.LZMAError, subprocess.SubprocessError, KeyError):
        parser.exit(2, "Static GPU analysis refused; check pin, ELF and fresh private output\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
