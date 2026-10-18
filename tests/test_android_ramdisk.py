"""The original guest archives must stay intact, including concatenated members."""

from pathlib import Path
import stat
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from android_ramdisk import PROPERTY_PATH, entries, lowram_archive, write_lowram


def archive(name, data, mode=stat.S_IFREG | 0o644):
    def entry(name, data, mode):
        encoded = name.encode() + b"\0"
        fields = [1, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0]
        result = b"070701" + "".join(f"{v:08x}" for v in fields).encode() + encoded
        result += b"\0" * (-len(result) % 4) + data
        return result + b"\0" * (-len(result) % 4)

    return entry(name, data, mode) + entry("TRAILER!!!", b"", 0)


class AndroidRamdisk(unittest.TestCase):
    def test_original_archives_unchanged(self):
        original = archive(PROPERTY_PATH, b"ro.debuggable=1\n") + archive("init", b"binary")
        result = lowram_archive(original)
        self.assertEqual(result[: len(original)], original)
        props = [data for name, mode, data in entries(result) if name == PROPERTY_PATH][-1]
        self.assertIn(b"ro.config.low_ram=true\n", props)
        self.assertIn(b"ro.debuggable=1\n", props)
        self.assertNotIn(b"ro.adb.secure=0", props)

    def test_truncated_payload_rejected(self):
        with self.assertRaises(ValueError):
            list(entries(archive("file", b"data")[:114]))

    def test_parent_path_rejected(self):
        with self.assertRaises(ValueError):
            list(entries(archive("../init", b"data")))

    def test_absolute_path_rejected(self):
        with self.assertRaises(ValueError):
            list(entries(archive("/init", b"data")))

    def test_missing_properties_rejected(self):
        with self.assertRaises(ValueError):
            lowram_archive(archive("init", b"binary"))

    def test_property_symlink_rejected(self):
        with self.assertRaises(ValueError):
            lowram_archive(archive(PROPERTY_PATH, b"other", stat.S_IFLNK | 0o777))

    def test_existing_destination_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "original"
            output = Path(directory) / "modified"
            source.write_bytes(archive(PROPERTY_PATH, b"ro.debuggable=1\n"))
            output.write_bytes(b"keep")
            with self.assertRaises(FileExistsError):
                write_lowram(source, output)
            self.assertEqual(output.read_bytes(), b"keep")
