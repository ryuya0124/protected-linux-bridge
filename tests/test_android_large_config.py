"""Separate disk markers and backing sizes must agree before either native variant ships."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidLargeConfigTest(unittest.TestCase):
    def compile_profile(self, memory, output):
        return subprocess.run(
            [
                "cc",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-DPLB_ANDROID_LARGE_MEMORY_MIB={memory}",
                "-I",
                str(ROOT / "tools"),
                str(ROOT / "tests/fixtures/android_large_config.c"),
                "-o",
                str(output),
            ],
            capture_output=True,
            text=True,
        )

    def test_independent_profile_markers_and_mapping_bounds(self):
        markers = set()
        with tempfile.TemporaryDirectory() as directory:
            for memory, library in (
                (1536, "libandroid-memory.so"),
                (1792, "libandroid-memory1792.so"),
            ):
                output = Path(directory) / str(memory)
                compiled = self.compile_profile(memory, output)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                fields = subprocess.check_output([str(output)], text=True).splitlines()
                self.assertEqual(
                    fields,
                    [
                        str(memory),
                        f"cf17-{memory}",
                        library,
                        f"plb-android-{memory}-v1",
                        f"size={memory}",
                        str((memory - 32) * 1024 * 1024),
                        str((memory + 768) * 1024),
                    ],
                )
                markers.add(fields[3])
        self.assertEqual(len(markers), 2)

    def test_unbounded_or_unknown_native_profiles_do_not_compile(self):
        with tempfile.TemporaryDirectory() as directory:
            for memory in (512, 2048, 6144):
                result = self.compile_profile(memory, Path(directory) / str(memory))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Only the separately prepared", result.stderr)
