"""Reject an incompatible descriptor before mapping it; no VM or device access."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PreallocatedRamContractTests(unittest.TestCase):
    def test_identity_access_size_and_seals(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "contract"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures/preallocated_ram_contract.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=3)


if __name__ == "__main__":
    unittest.main()
