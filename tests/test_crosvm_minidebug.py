"""Synthetic ELF metadata only. No ARM execution, VMM launch, device or large RAM test."""

import lzma
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_crosvm_minidebug import (
    HEADER,
    SECTION,
    FUNCTIONS,
    MAX_DEBUG,
    sections,
    extract_debug,
    inspect,
    select_functions,
    write_private,
)


def elf(extra=None):
    entries = [(".text", bytes(64))]
    if extra is not None:
        entries.append((".gnu_debugdata", extra))
    names = b"\0.shstrtab\0"
    offsets = {}
    for name, _ in entries:
        offsets[name] = len(names)
        names += name.encode() + b"\0"
    payload = bytearray(HEADER.size)
    names_at = len(payload)
    payload += names
    table = [(0,) * 10, (1, 3, 0, 0, names_at, len(names), 0, 0, 1, 0)]
    for name, content in entries:
        at = len(payload)
        payload += content
        table.append(
            (offsets[name], 1, 0, 0x1000 if name == ".text" else 0, at, len(content), 0, 0, 1, 0)
        )
    table_at = len(payload)
    for entry in table:
        payload += SECTION.pack(*entry)
    payload[: HEADER.size] = HEADER.pack(
        b"\x7fELF\x02\x01\x01" + bytes(9),
        3,
        183,
        1,
        0,
        0,
        table_at,
        0,
        HEADER.size,
        0,
        0,
        SECTION.size,
        len(table),
        1,
    )
    return bytes(payload)


def nm_text():
    return "\n".join(
        f"{0x1000 + index * 4:016x} 0000000000000004 T {name}"
        for index, name in enumerate(FUNCTIONS.values())
    )


class CrosvmMiniDebugTest(unittest.TestCase):
    def test_extracts_exact_nested_elf_metadata(self):
        debug = elf()
        image = elf(lzma.compress(debug))
        self.assertEqual(extract_debug(image), debug)
        self.assertEqual(len(select_functions(nm_text(), image)), 3)

    def test_rejects_truncation_architecture_and_section_table_overflow(self):
        original = elf()
        wrong_arch = bytearray(original)
        struct.pack_into("<H", wrong_arch, 18, 62)
        wrong_table = bytearray(original)
        struct.pack_into("<Q", wrong_table, 40, len(original))
        for data in (b"ELF", original[:40], wrong_arch, wrong_table):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                sections(data)

    def test_rejects_missing_or_trailing_minidebug(self):
        with self.assertRaises(ValueError):
            extract_debug(elf())
        with self.assertRaises(ValueError):
            extract_debug(elf(lzma.compress(elf()) + b"trailing"))

    def test_bounds_decompression_before_parsing_nested_sections(self):
        compressed = lzma.compress(bytes(MAX_DEBUG + 1))
        with self.assertRaises(ValueError):
            extract_debug(elf(compressed))

    def test_rejects_missing_duplicate_or_out_of_text_function_symbols(self):
        valid = nm_text()
        for text in (
            "",
            valid + "\n" + valid.splitlines()[0],
            valid.replace("0000000000001000", "0000000000002000"),
            valid.replace("0000000000001000", "0000000000001001"),
        ):
            with self.subTest(text=text[:20]), self.assertRaises(ValueError):
                select_functions(text, elf())

    def test_private_output_never_overwrites_or_follows_a_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "original"
            write_private(path, b"keep")
            with self.assertRaises(FileExistsError):
                write_private(path, b"replace")
            alias = Path(directory) / "alias"
            alias.symlink_to(path)
            with self.assertRaises(FileExistsError):
                write_private(alias, b"replace")
            self.assertEqual(path.read_bytes(), b"keep")
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_wrong_image_pin_stops_before_output_or_external_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "image"
            source.write_bytes(elf(lzma.compress(elf())))
            output = Path(directory) / "output"
            with patch("inspect_crosvm_minidebug.subprocess.check_output") as command:
                with self.assertRaises(ValueError):
                    inspect(source, output, Path(directory))
                command.assert_not_called()
            self.assertFalse(output.exists())

    def test_image_symlink_is_not_followed(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "image"
            source.write_bytes(b"fixture")
            alias = Path(directory) / "alias"
            alias.symlink_to(source)
            with self.assertRaises(OSError):
                inspect(alias, Path(directory) / "output", Path(directory))
