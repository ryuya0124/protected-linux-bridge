"""The app's offline clone must preserve originals and reject unsafe file shapes."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidGpu4gCopyTest(unittest.TestCase):
    def test_verified_clone_and_refusal_paths_preserve_existing_data(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            executable = folder / "copy"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-function",
                    "-I",
                    str(ROOT / "native"),
                    str(ROOT / "tests/fixtures/android_gpu4g_copy.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
            )
            original, copied = folder / "original", folder / "copied"
            content = os.urandom(180000)
            original.write_bytes(content)
            original.chmod(0o600)

            def copy(source, destination):
                return subprocess.run(
                    [str(executable), str(source), str(destination)], timeout=5
                ).returncode

            self.assertEqual(copy(original, copied), 0)
            self.assertEqual(original.read_bytes(), content)
            self.assertEqual(copied.read_bytes(), content)
            self.assertEqual(copied.stat().st_mode & 0o777, 0o600)
            copied.write_bytes(b"retain existing destination")
            self.assertNotEqual(copy(original, copied), 0)
            self.assertEqual(copied.read_bytes(), b"retain existing destination")
            link = folder / "symlink"
            link.symlink_to(original)
            self.assertNotEqual(copy(link, folder / "new"), 0)
            self.assertNotEqual(copy(original, link), 0)
            self.assertFalse((folder / "new").exists())
            hardlink = folder / "hardlink"
            os.link(original, hardlink)
            self.assertNotEqual(copy(original, folder / "new"), 0)
            hardlink.unlink()
            original.chmod(0o644)
            self.assertNotEqual(copy(original, folder / "new"), 0)
            self.assertEqual(original.read_bytes(), content)
            self.assertFalse((folder / "new").exists())
