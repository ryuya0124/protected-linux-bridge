"""The raw frame boundary must reject unknown/oversized screenshot formats."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidGuestFrameTest(unittest.TestCase):
    def test_real_frame_validator_rejects_bad_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "frame"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/android_guest_frame.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run([str(executable)], check=True, timeout=5)
