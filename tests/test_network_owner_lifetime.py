"""Network cleanup must finish before the shared VM owner lock can be acquired."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NetworkOwnerLifetimeTest(unittest.TestCase):
    def test_manager_exit_does_not_admit_another_vm(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "owner-lifetime"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures/network_owner_lifetime.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
            )
            result = subprocess.run(
                [str(binary), str(Path(directory) / "vm-owner.lock")],
                check=True,
                capture_output=True,
                timeout=5,
            )
            self.assertIn(b"PLB_NETWORK_OWNER_LIFETIME_OK", result.stdout)
