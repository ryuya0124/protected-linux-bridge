"""Guest GPT containers must retain checked headers and unchanged input files."""

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_android_probe import vbmeta_boot_info, write_gpt


class AndroidProbeDisk(unittest.TestCase):
    def test_vbmeta_summary_includes_boot_footer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = bytearray(256)
            header[:4] = b"AVB0"
            (root / "vbmeta.img").write_bytes(header)
            boot = bytearray(1024)
            boot[512:768] = header
            boot[-64:-60] = b"AVBf"
            struct.pack_into(">2Q", boot, len(boot) - 64 + 20, 512, 256)
            (root / "boot.img").write_bytes(boot)
            actual = vbmeta_boot_info(root, ["vbmeta.img", "boot.img"])
            self.assertEqual(actual["size"], 512)
            self.assertEqual(actual["digest"], hashlib.sha256(header + header).hexdigest())

    def test_vbmeta_outside_image_or_disabled_verification_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = bytearray(256)
            header[:4] = b"AVB0"
            struct.pack_into(">I", header, 120, 2)
            (root / "vbmeta.img").write_bytes(header)
            with self.assertRaises(ValueError):
                vbmeta_boot_info(root, ["vbmeta.img"])
            struct.pack_into(">I", header, 120, 0)
            struct.pack_into(">Q", header, 12, 4096)
            (root / "vbmeta.img").write_bytes(header)
            with self.assertRaises(ValueError):
                vbmeta_boot_info(root, ["vbmeta.img"])

    def test_headers_partitions_and_readonly_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "guest-payload.img"
            source.write_bytes(b"PLB guest payload" + bytes(4096 - 17))
            original = hashlib.sha256(source.read_bytes()).digest()
            output = root / "new-guest-disk.raw"
            write_gpt(output, [("metadata", source, 4096)])
            data = output.read_bytes()
            self.assertEqual(data[510:512], b"\x55\xaa")
            for sector in (1, len(data) // 512 - 1):
                header = bytearray(data[sector * 512 : sector * 512 + 92])
                self.assertEqual(header[:8], b"EFI PART")
                expected = struct.unpack_from("<I", header, 16)[0]
                struct.pack_into("<I", header, 16, 0)
                self.assertEqual(zlib.crc32(header), expected)
                table_sector = struct.unpack_from("<Q", header, 72)[0]
                table = data[table_sector * 512 : table_sector * 512 + 128 * 128]
                self.assertEqual(zlib.crc32(table), struct.unpack_from("<I", header, 88)[0])
                self.assertEqual(table[56:72].decode("utf-16le").rstrip("\0"), "metadata")
                start, end = struct.unpack_from("<2Q", table, 32)
                self.assertEqual((end - start + 1) * 512, 4096)
                self.assertEqual(data[start * 512 : (end + 1) * 512], source.read_bytes())
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), original)

    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "source", root / "existing"
            source.write_bytes(b"guest")
            output.write_bytes(b"preserve")
            with self.assertRaises(FileExistsError):
                write_gpt(output, [("guest", source, 5)])
            self.assertEqual(output.read_bytes(), b"preserve")


if __name__ == "__main__":
    unittest.main()
