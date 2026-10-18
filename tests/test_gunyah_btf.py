"""Only parse bounded metadata; no live kernel or BPF access in these tests."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_gunyah_btf import inspect


def blob(types, strings):
    return (
        struct.pack("<HBB5I", 0xEB9F, 1, 0, 24, 0, len(types), len(types), len(strings))
        + types
        + strings
    )


class GunyahMetadata(unittest.TestCase):
    def test_actual_error_disambiguated_from_rm_enum(self):
        names = (
            b"\0gunyah_error\0GUNYAH_ERROR_NOMEM\0gunyah_rm_error\0GUNYAH_RM_ERROR_MEM_INVALID\0"
        )
        enum = struct.pack("<3I", 1, 6 << 24 | 1, 4) + struct.pack("<2I", 14, 10)
        rm = struct.pack("<3I", 33, 6 << 24 | 1, 4) + struct.pack("<2I", 49, 10)
        result = inspect(blob(enum + rm, names))
        self.assertEqual(result["enums"], {"gunyah_error": {"GUNYAH_ERROR_NOMEM": 10}})

    def test_signed_error_values(self):
        names = b"\0gunyah_error\0negative\0"
        enum = struct.pack("<3I", 1, 1 << 31 | 6 << 24 | 1, 4) + struct.pack("<Ii", 14, -2)
        self.assertEqual(inspect(blob(enum, names))["enums"]["gunyah_error"]["negative"], -2)

    def test_structure_bitfield_width_is_not_an_offset(self):
        names = b"\0gunyah_vm\0member\0"
        structure = struct.pack("<3I", 1, 1 << 31 | 4 << 24 | 1, 8)
        structure += struct.pack("<3I", 11, 1, 3 << 24 | 16)
        member = inspect(blob(structure, names))["structures"]["gunyah_vm"]["members"][0]
        self.assertEqual(member, {"name": "member", "bit_offset": 16})

    def test_structure_without_bitfield_preserves_full_offset(self):
        names = b"\0gunyah_vm\0member\0"
        structure = struct.pack("<3I", 1, 4 << 24 | 1, 0x4000000)
        structure += struct.pack("<3I", 11, 1, 0x1000010)
        member = inspect(blob(structure, names))["structures"]["gunyah_vm"]["members"][0]
        self.assertEqual(member["bit_offset"], 0x1000010)

    def test_empty_types(self):
        self.assertEqual(inspect(blob(b"", b"\0")), {"enums": {}, "structures": {}})

    def test_short_header(self):
        with self.assertRaises(ValueError):
            inspect(b"BTF")

    def test_section_outside_file(self):
        with self.assertRaises(ValueError):
            inspect(blob(b"", b"\0")[:-1])

    def test_unterminated_strings(self):
        with self.assertRaises(ValueError):
            inspect(blob(b"", b"\0bad"))

    def test_unknown_kind(self):
        with self.assertRaises(ValueError):
            inspect(blob(struct.pack("<3I", 0, 31 << 24, 0), b"\0"))

    def test_invalid_name_offset(self):
        with self.assertRaises(ValueError):
            inspect(blob(struct.pack("<3I", 9, 2 << 24, 0), b"\0"))

    def test_truncated_type_payload(self):
        with self.assertRaises(ValueError):
            inspect(blob(struct.pack("<3I", 0, 6 << 24 | 1, 4), b"\0"))
