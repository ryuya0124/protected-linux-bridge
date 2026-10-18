"""A diagnostic timeout must stop its own worker even when alarms are blocked."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProbeWatchdogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / "watchdog"
        subprocess.run(
            [
                "cc",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "tools"),
                str(ROOT / "tests/fixtures/probe_watchdog.c"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_only_owned_child_is_terminated(self):
        for mode in ("success", "signal", "blocked-alarm", "invalid"):
            with self.subTest(mode=mode):
                subprocess.run([str(self.binary), mode], check=True, timeout=4)


if __name__ == "__main__":
    unittest.main()
