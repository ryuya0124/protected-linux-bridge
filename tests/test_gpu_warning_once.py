"""The explicit single-trial artifact cannot silently grant another boot/run."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GpuWarningOnceTests(unittest.TestCase):
    def test_builder_rejects_broader_or_incomplete_exception_before_sdk_access(self):
        approval = (
            "--approved-warning-boot-id",
            "00000000-0000-0000-0000-000000000000",
            "--approved-warning-issued-ms",
            "1",
        )
        for arguments in (
            approval,
            approval + ("--guest-render-apk", "unused.apk", "--memory", "512"),
            approval + ("--guest-render-apk", "unused.apk", "--memory", "1536", "--mode", "vulkan"),
            approval[:2],
            approval[2:],
        ):
            with self.subTest(arguments=arguments):
                result = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "tools/build_android_gpu_probe.py"),
                        "--sdk",
                        "/nonexistent-sdk",
                        *arguments,
                    ],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_time_boot_taint_inherited_file_and_one_use_counter(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / "gpu_once.generated.h").write_text(
                '#define PLB_GPU_ONCE_BOOT_ID "fixture-boot"\n'
                "#define PLB_GPU_ONCE_ISSUED_MS 1000ULL\n"
                "#define PLB_GPU_ONCE_DEADLINE_MS 301000ULL\n"
                "#define PLB_GPU_ONCE_TAINT 4640UL\n"
            )
            target = directory / "approval-test"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    "-I",
                    str(directory),
                    f'-DPLB_GPU_ONCE_MARKER="{directory / "used"}"',
                    str(ROOT / "tests/fixtures/gpu_warning_once.c"),
                    "-o",
                    str(target),
                ],
                check=True,
            )
            subprocess.run([str(target)], check=True)
            self.assertEqual((directory / "used").read_text(), "PLB_GPU_ONCE 1536 GLES\n")
