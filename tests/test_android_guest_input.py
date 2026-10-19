"""Validate the guest input command boundary using the real native builder."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidGuestInputTest(unittest.TestCase):
    def test_fixed_commands_reject_invalid_coordinates_keys_and_duration(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "input"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/android_guest_input.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run([str(executable)], check=True, timeout=5)
