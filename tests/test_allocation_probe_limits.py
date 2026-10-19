"""A removed pressure mode cannot exceed the user's six-GiB diagnostic limit."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AllocationProbeLimitTests(unittest.TestCase):
    def test_combined_budgets_removed_modes_and_partial_final_batch(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "allocation-limits"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures/allocation_probe_limits.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
