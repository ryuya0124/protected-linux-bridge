"""The experimental Surface profile must keep every writable VM path separate."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMMON = [
    "PLB_ANDROID4G_INTERACTIVE",
    "PLB_ANDROID4G_CUSTOM_GUEST",
    "PLB_ANDROID4G_LARGER_DMA_POOL",
    "PLB_PREALLOCATED_ANDROID_PROXY",
]


class AndroidSurfaceProfileTest(unittest.TestCase):
    def compile(self, output, modes):
        return subprocess.run(
            [
                "cc",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "tools"),
                *["-D" + mode + "=1" for mode in modes],
                str(ROOT / "tests/fixtures/android_surface_arguments.c"),
                "-o",
                str(output),
            ],
            capture_output=True,
            text=True,
        )

    def test_all_writable_paths_are_separate_and_protection_is_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "args"
            profiles = []
            for modes in [
                COMMON,
                COMMON + ["PLB_ANDROID4G_SURFACE_INTERACTIVE"],
                COMMON + ["PLB_ANDROID4G_SURFACE_INTERACTIVE", "PLB_SURFACE_PRODUCT"],
            ]:
                result = self.compile(binary, modes)
                self.assertEqual(result.returncode, 0, result.stderr)
                args = subprocess.check_output([str(binary)], text=True).splitlines()
                self.assertIn("--protected-vm-without-firmware", args)
                self.assertEqual(args[args.index("--mem") + 1], "size=4096")
                self.assertEqual(args[args.index("--swiotlb") + 1], "128")
                profiles.append(args)
            baseline, surface, product = profiles
            position = product.index("--input")
            self.assertEqual(
                product[position + 1],
                "multi-touch[path=/owned/app/android-guests/cf17-gpu4096-surface/touch.sock,width=480,height=800]",
            )
            self.assertEqual(product[:position] + product[position + 2 :], surface)
            baseline_blocks = {
                baseline[i + 1] for i, value in enumerate(baseline) if value == "--block"
            }
            surface_blocks = {
                surface[i + 1] for i, value in enumerate(surface) if value == "--block"
            }
            self.assertEqual(len(surface_blocks), 3)
            self.assertTrue(baseline_blocks.isdisjoint(surface_blocks))
            self.assertTrue(all("/cf17-gpu4096-surface/" in value for value in surface_blocks))
            self.assertNotEqual(baseline[0], surface[0])
            self.assertNotEqual(baseline[-1], surface[-1])
            for value in surface:
                self.assertNotIn("/cf17-gpu4096/", value)

    def test_surface_mode_refuses_missing_interactive_or_custom_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            for modes in [[], ["PLB_ANDROID4G_INTERACTIVE"], ["PLB_ANDROID4G_CUSTOM_GUEST"]]:
                result = self.compile(
                    Path(directory) / "args", modes + ["PLB_ANDROID4G_SURFACE_INTERACTIVE"]
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Surface mode requires", result.stderr)
