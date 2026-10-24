"""Exercise the same bounded file reader linked into the guest UEFI loader."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeUefiFileReadTest(unittest.TestCase):
    def test_large_and_short_reads_eof_and_error_progress(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "reader"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-O1",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    str(ROOT / "tests/fixtures/uefi_file_read.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=10)
