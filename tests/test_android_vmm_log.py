"""A frozen UI consumer must never block owned VM log draining or fault detection."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidVmmLogTest(unittest.TestCase):
    def test_full_ui_pipe_does_not_block_bounded_logging(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "log"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pthread",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/android_vmm_log.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                [str(executable), str(Path(directory) / "vmm.log")], check=True, timeout=10
            )
