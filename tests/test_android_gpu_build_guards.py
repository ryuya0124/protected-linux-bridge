"""Compile the actual diagnostic combinations without executing a VMM or GPU driver."""

from pathlib import Path
import os
import platform
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
HOST = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
COMPILER = (
    SDK / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / HOST / "bin/aarch64-linux-android35-clang"
)


@unittest.skipUnless(COMPILER.is_file(), "Requires the pinned Android NDK")
class AndroidGpuBuildGuardsTest(unittest.TestCase):
    def compile_manager(self, extra=()):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "android_guest_setup.generated.h").write_text(
                'static const char android_setup_service[] = "exec:true";\n'
            )
            (output / "android_gpu_early.generated.h").write_text(
                'static const char console_swap_command[] = "true\\n";\n'
            )
            return subprocess.run(
                [
                    str(COMPILER),
                    "-fsyntax-only",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(output),
                    "-DPLB_ANDROID_MEMORY_PROBE=1",
                    "-DPLB_ANDROID_MEMORY_MIB=1536",
                    "-DPLB_ANDROID_MEMORY_POLICY=1",
                    "-DPLB_ANDROID_GFXSTREAM_PROBE=1",
                    "-DPLB_ANDROID_GPU_MEMORY_PROBE=1",
                    '-DPLB_ANDROID_MEMORY_LIBRARY_SHA256="' + "0" * 64 + '"',
                    *extra,
                    str(ROOT / "tools/android_probe_runner.c"),
                ],
                capture_output=True,
                text=True,
            )

    def test_bounded_gles1536_manager_compiles(self):
        result = self.compile_manager()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_larger_gpu_size_vulkan_and_missing_policy_are_rejected(self):
        for extra in (
            ("-UPLB_ANDROID_MEMORY_MIB", "-DPLB_ANDROID_MEMORY_MIB=1792"),
            ("-DPLB_ANDROID_GFXSTREAM_VULKAN_PROBE=1",),
            ("-UPLB_ANDROID_MEMORY_POLICY",),
        ):
            with self.subTest(extra=extra):
                result = self.compile_manager(extra)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("restricted to the pinned 1536 MiB GLES profile", result.stderr)

    def test_production_and_gpu_backings_exclude_refault_and_ioctl_interposition(self):
        for mode in ("PLB_ANDROID_LARGE_BACKING", "PLB_ANDROID_GPU_BACKING"):
            result = subprocess.check_output(
                [
                    str(COMPILER),
                    "-E",
                    "-P",
                    f"-D{mode}=1",
                    str(ROOT / "tools/memory_backing_probe.c"),
                ],
                text=True,
            )
            self.assertNotIn("plb_refault_owned_shmem", result)
            self.assertNotIn("struct observed_memory_region", result)
            self.assertIn("ANDROID_BACKING_UNSUITABLE", result)
            self.assertIn("plb_host_memory_healthy()", result)
