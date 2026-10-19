"""A guest DMA pool selection must preserve total RAM and protected operation."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Android4gDmaPoolTest(unittest.TestCase):
    def compile(self, output, defines):
        return subprocess.run(
            [
                "cc",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "tools"),
                *["-D" + name + "=1" for name in defines],
                str(ROOT / "tests/fixtures/android4g_arguments.c"),
                "-o",
                str(output),
            ],
            capture_output=True,
            text=True,
        )

    def test_dma_pool_is_within_the_same_protected_four_gib(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "arguments"
            for pool in (32, 128):
                defines = (
                    []
                    if pool == 32
                    else [
                        "PLB_ANDROID4G_LARGER_DMA_POOL",
                        "PLB_PREALLOCATED_ANDROID_PROXY",
                        "PLB_ANDROID4G_CUSTOM_GUEST",
                    ]
                )
                compiled = self.compile(output, defines)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                args = subprocess.check_output([str(output)], text=True).splitlines()
                self.assertEqual(
                    args,
                    [
                        "--protected-vm-without-firmware",
                        "--swiotlb",
                        str(pool),
                        "--mem",
                        "size=4096",
                    ],
                )

    def test_larger_pool_rejects_missing_diagnostic_or_custom_guest(self):
        with tempfile.TemporaryDirectory() as directory:
            for other in ([], ["PLB_PREALLOCATED_ANDROID_PROXY"], ["PLB_ANDROID4G_CUSTOM_GUEST"]):
                result = self.compile(
                    Path(directory) / "arguments", ["PLB_ANDROID4G_LARGER_DMA_POOL", *other]
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Larger DMA pool requires", result.stderr)
