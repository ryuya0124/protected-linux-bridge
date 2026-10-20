"""Guest sensor protocol authorization, timestamps and actual framed data."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidGuestSensorsTest(unittest.TestCase):
    def test_sensor_wire_protocol_and_revocation(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "sensors"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-D_GNU_SOURCE",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/android_guest_sensors.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
