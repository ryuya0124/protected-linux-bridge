"""The real manager admission reply refuses active roles and aborted requests."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class IdleHandoffTest(unittest.TestCase):
    def test_authority_active_roles_and_disconnected_client(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "idle-handoff"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/idle_handoff.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            result = subprocess.run([str(binary)], capture_output=True, check=True, timeout=3)
            self.assertIn(b"PLB_IDLE_HANDOFF_BOUNDARIES_PASS", result.stdout)
