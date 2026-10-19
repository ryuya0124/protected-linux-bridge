"""A pending setup, success, refusal and child EOF use the real nonblocking reader."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidNetworkAsyncTest(unittest.TestCase):
    def test_pending_does_not_block_input_and_eof_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "async"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/android_network_async.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, timeout=3)
            self.assertIn(b"PLB_NETWORK_ASYNC_OK", result.stdout)
