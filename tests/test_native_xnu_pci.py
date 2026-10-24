"""Exercise the real protocol core with malformed PCI metadata and a fake block backend."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeXnuPciTest(unittest.TestCase):
    def test_protocol_boundaries_and_read_only_queue(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "probe"
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
                    str(ROOT / "tests/fixtures/virtio_pci_block.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("fault controls passed", result.stdout)
