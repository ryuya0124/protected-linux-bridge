"""The relay closes VM-like descriptors only in its child and refuses missing procfs."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NetworkFdBoundaryTest(unittest.TestCase):
    def test_child_descriptor_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "boundary"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/network_fd_boundary.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, timeout=5)
            self.assertIn(b"PLB_NETWORK_FD_BOUNDARY_OK", result.stdout)
