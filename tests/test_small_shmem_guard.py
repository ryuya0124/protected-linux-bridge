"""The integrated startup guardian restores files after failure and deadlines."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SmallShmemGuardTests(unittest.TestCase):
    def test_completion_controller_death_deadline_and_unexpected_values(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            binary = directory / "guard-test"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native"),
                    f'-DPLB_SHMEM_GUARD_TEST_PREFIX="{directory}"',
                    str(ROOT / "tests/fixtures/small_shmem_guard.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            for case in (
                "normal",
                "controller-death",
                "deadline",
                "refuse-original",
                "concurrent-change",
            ):
                with self.subTest(case=case):
                    expected = [
                        "never",
                        "always" if case == "refuse-original" else "never",
                        "never",
                    ]
                    for size, mode in zip((16, 32, 64), expected):
                        (directory / f"small-{size}k").write_text(mode)
                    subprocess.run([str(binary), case], capture_output=True, timeout=5, check=True)
                    if case == "concurrent-change":
                        expected[1] = "always"
                    self.assertEqual(
                        [(directory / f"small-{size}k").read_text() for size in (16, 32, 64)],
                        expected,
                    )
