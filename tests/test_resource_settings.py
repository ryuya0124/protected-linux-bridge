"""Validate the production resource-file parser before values reach crosvm."""

from pathlib import Path
import subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class ResourceSettingsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        folder = Path(cls.tmp.name)
        source = folder / "resource.c"
        source.write_text((ROOT / "tests/fixtures/resource_settings.c").read_text())
        cls.probe = folder / "resource"
        subprocess.run(
            ["cc", "-O2", "-I", str(ROOT / "native"), str(source), "-o", str(cls.probe)], check=True
        )

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def parse(self, value):
        return subprocess.run([str(self.probe)], input=value).returncode == 0

    def test_all_supported_resource_options(self):
        for ram in (512, 1024, 2048, 4096, 8192):
            for cpu in (1, 2, 4, 8):
                for minutes in (0, 15, 30, 60, 120, 360):
                    self.assertTrue(self.parse(f"{ram} {cpu} {minutes} 0 1 1\n".encode()))

    def test_custom_disk_capacities(self):
        for gib in (1, 2, 3, 33, 64, 128):
            self.assertTrue(self.parse(f"512 1 30 {gib} 0 0\n".encode()))

    def test_reject_invalid_or_ambiguous_files(self):
        for value in (
            b"",
            b"512 1 30 0 0 0",
            b"512 1 30 0 0 0\n\n",
            b"512 1 30 0 0 0\nX",
            b"512 1 30 0 0 0\n\x00",
            b"512 1 30 0 0 0\x00\n",
            b"-512 1 30 0 0 0\n",
            b"4294967808 1 30 0 0 0\n",
            b"512 16 30 0 0 0\n",
            b"512 1 31 0 0 0\n",
            b"512 1 30 129 0 0\n",
            b"512 1 30 0 2 0\n",
            b"512 1 30 0 0 2\n",
            b" 512 1 30 0 0 0\n",
            b"9" * 200,
        ):
            with self.subTest(value=value):
                self.assertFalse(self.parse(value))
