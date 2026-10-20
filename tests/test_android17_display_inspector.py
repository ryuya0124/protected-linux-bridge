"""Synthetic dynamic linkage and function metadata; no ARM/GPU execution."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import inspect_android17_display as display
from inspect_crosvm_minidebug import HEADER, SECTION


def dynamic_elf(binding=1, visibility=0, defined=True, pie=True, linked_index=3):
    names = b"\0stream_renderer_init\0libnativewindow.so\0"
    symbols = bytes(display.SYMBOL.size) + display.SYMBOL.pack(
        1, (binding << 4) | 2, visibility, int(defined), 0x1000, 4
    )
    needed = names.index(b"libnativewindow.so")
    dynamic = b"".join(
        display.DYNAMIC.pack(*entry)
        for entry in [(1, needed), (0x6FFFFFFB, 0x08000000 if pie else 0), (0, 0)]
    )
    payloads = [
        (".text", bytes(64), 1, 0, 0),
        (".dynstr", names, 3, 0, 0),
        (".dynsym", symbols, 11, linked_index, display.SYMBOL.size),
        (".dynamic", dynamic, 6, 3, display.DYNAMIC.size),
    ]
    strings = b"\0.shstrtab\0"
    offsets = {}
    for name, *_ in payloads:
        offsets[name] = len(strings)
        strings += name.encode() + b"\0"
    data = bytearray(HEADER.size)
    string_offset = len(data)
    data += strings
    table = [(0,) * 10, (1, 3, 0, 0, string_offset, len(strings), 0, 0, 1, 0)]
    for name, payload, kind, linked, size in payloads:
        offset = len(data)
        data += payload
        table.append(
            (
                offsets[name],
                kind,
                0,
                0x1000 if name == ".text" else 0,
                offset,
                len(payload),
                linked,
                0,
                1,
                size,
            )
        )
    table_offset = len(data)
    data += b"".join(SECTION.pack(*entry) for entry in table)
    data[: HEADER.size] = HEADER.pack(
        b"\x7fELF\x02\x01\x01" + bytes(9),
        3,
        183,
        1,
        0,
        0,
        table_offset,
        0,
        HEADER.size,
        0,
        0,
        SECTION.size,
        len(table),
        1,
    )
    return bytes(data)


class Android17DisplayInspectorTest(unittest.TestCase):
    def test_pie_and_dynamic_exports_are_separate_properties(self):
        result = display.linkage(dynamic_elf())
        self.assertTrue(result["df_1_pie"])
        self.assertEqual(result["needed_libraries"], ["libnativewindow.so"])
        self.assertEqual(result["dynamic_gpu_exports"], ["stream_renderer_init"])
        self.assertFalse(display.linkage(dynamic_elf(pie=False))["df_1_pie"])

    def test_undefined_local_and_hidden_names_are_not_dynamic_exports(self):
        for options in [{"defined": False}, {"binding": 0}, {"visibility": 2}]:
            with self.subTest(options=options):
                self.assertEqual(display.linkage(dynamic_elf(**options))["dynamic_gpu_exports"], [])

    def test_weak_protected_definition_is_exported(self):
        self.assertEqual(
            display.linkage(dynamic_elf(binding=2, visibility=3))["dynamic_gpu_exports"],
            ["stream_renderer_init"],
        )

    def test_invalid_string_link_truncation_and_names_refused(self):
        invalid_name = bytearray(dynamic_elf())
        symbols = display.sections(invalid_name)[".dynsym"]
        struct.pack_into("<I", invalid_name, symbols[4] + display.SYMBOL.size, 0xFFFFFFFF)
        for data in [dynamic_elf(linked_index=2), dynamic_elf()[:40], invalid_name]:
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                display.linkage(data)

    def test_dynamic_table_requires_terminator(self):
        data = bytearray(dynamic_elf())
        entry = display.sections(data)[".dynamic"]
        struct.pack_into("<q", data, entry[4] + 2 * display.DYNAMIC.size, 2)
        with self.assertRaises(ValueError):
            display.linkage(data)

    def test_dynamic_section_kind_and_string_link_must_agree(self):
        for field, value in [(1, 1), (6, 2)]:
            data = bytearray(dynamic_elf())
            header = HEADER.unpack_from(data)
            offset = header[6] + 5 * SECTION.size
            entry = list(SECTION.unpack_from(data, offset))
            entry[field] = value
            SECTION.pack_into(data, offset, *entry)
            with self.assertRaises(ValueError):
                display.linkage(data)

    def test_function_evidence_requires_complete_unambiguous_in_text_bodies(self):
        lines = [
            f"{0x1000 + 4 * i:016x} 00000004 T {name}"
            for i, name in enumerate(display.FUNCTIONS.values())
        ]
        text = "\n".join(lines)
        self.assertEqual(len(display.select_functions(text, dynamic_elf())), 7)
        for wrong in [
            "\n".join(lines[:-1]),
            text + "\n" + lines[0],
            text.replace("0000000000001000", "0000000000002000"),
        ]:
            with self.assertRaises(ValueError):
                display.select_functions(wrong, dynamic_elf())

    def test_wrong_pin_or_symlink_never_reaches_external_tool(self):
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder) / "image"
            image.write_bytes(dynamic_elf())
            alias = Path(folder) / "alias"
            alias.symlink_to(image)
            with patch.object(display.subprocess, "check_output") as command:
                for path in [image, alias]:
                    with self.assertRaises((ValueError, OSError)):
                        display.inspect(path, Path(folder) / "output", Path(folder))
                command.assert_not_called()


if __name__ == "__main__":
    unittest.main()
