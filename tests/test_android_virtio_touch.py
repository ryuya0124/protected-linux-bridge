"""The native guest touch encoder batches fingers and rejects malformed transitions."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidVirtioTouchTest(unittest.TestCase):
    def test_state_and_wire_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "touch"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/android_virtio_touch.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
            )
            result = subprocess.run([str(executable)], check=True, capture_output=True, timeout=5)
            self.assertIn(b"PLB_VIRTIO_MULTITOUCH_OK", result.stdout)
