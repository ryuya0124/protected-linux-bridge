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
    def compile_manager(self, extra=(), adopted=False):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "android_guest_setup.generated.h").write_text(
                'static const char android_setup_service[] = "exec:true";\n'
            )
            (output / "android_gpu_early.generated.h").write_text(
                'static const char console_swap_command[] = "true\\n";\n'
            )
            (output / "android_gpu_probe.generated.h").write_text(
                "\n".join(
                    f'static const char gpu_probe_{name}[] = "exec:true";'
                    for name in ("begin", "finish", "cleanup", "collect", "poweroff")
                )
                + '\nstatic const char *const gpu_probe_chunks[] = {"exec:true"};\n'
            )
            (output / "android4g.generated.h").write_text(
                '#define PLB_ANDROID4G_VMM_SHA256 "' + "0" * 64 + '"\n'
                '#define PLB_ANDROID4G_PROXY_SHA256 "' + "0" * 64 + '"\n'
                '#define PLB_ANDROID4G_KERNEL_SHA256 "' + "0" * 64 + '"\n'
                '#define PLB_ANDROID4G_INITRAMFS_SHA256 "' + "0" * 64 + '"\n'
                '#define PLB_ANDROID4G_COMPAT_SHA256 "' + "0" * 64 + '"\n'
            )
            modes = (
                [
                    "-DPLB_ANDROID_ADOPTED_PROBE=1",
                    "-DPLB_ANDROID_GFXSTREAM_PROBE=1",
                    "-DPLB_ANDROID_GPU_RENDER_PROBE=1",
                ]
                if adopted
                else [
                    "-DPLB_ANDROID_MEMORY_PROBE=1",
                    "-DPLB_ANDROID_MEMORY_MIB=1536",
                    "-DPLB_ANDROID_MEMORY_POLICY=1",
                    "-DPLB_ANDROID_GFXSTREAM_PROBE=1",
                    "-DPLB_ANDROID_GPU_MEMORY_PROBE=1",
                    '-DPLB_ANDROID_MEMORY_LIBRARY_SHA256="' + "0" * 64 + '"',
                ]
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
                    *modes,
                    *extra,
                    str(ROOT / "tools/android_probe_runner.c"),
                ],
                capture_output=True,
                text=True,
            )

    def test_adopted_4g_gles_compiles_and_rejects_hybrid_or_missing_renderer(self):
        result = self.compile_manager(adopted=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        for extra in (
            ("-DPLB_ANDROID_MEMORY_PROBE=1",),
            ("-DPLB_ANDROID_LARGE_PROFILE=1",),
            ("-DPLB_ANDROID_GFXSTREAM_VULKAN_PROBE=1",),
            ("-DPLB_GPU_WARNING_ONCE=1",),
            ("-UPLB_ANDROID_GFXSTREAM_PROBE",),
            ("-UPLB_ANDROID_GPU_RENDER_PROBE",),
        ):
            with self.subTest(extra=extra):
                result = self.compile_manager(extra, adopted=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("isolated healthy-boot 4 GiB GLES diagnostic", result.stderr)

    def test_interactive_gpu_uses_the_custom_guest_without_diagnostic_payload(self):
        flags = (
            "-DPLB_ANDROID4G_INTERACTIVE=1",
            "-DPLB_ANDROID4G_CUSTOM_GUEST=1",
            "-UPLB_ANDROID_GPU_RENDER_PROBE",
        )
        result = self.compile_manager(flags, adopted=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        for extra in (("-UPLB_ANDROID4G_CUSTOM_GUEST",), ("-DPLB_ANDROID_GPU_RENDER_PROBE=1",)):
            result = self.compile_manager(flags + extra, adopted=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Interactive GPU requires", result.stderr)
        result = self.compile_manager(flags)
        self.assertNotEqual(result.returncode, 0)

    def test_bounded_gles1536_manager_compiles(self):
        result = self.compile_manager()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_custom_kernel_requires_the_separate_adopted_profile(self):
        result = self.compile_manager(("-DPLB_ANDROID4G_CUSTOM_GUEST=1",), adopted=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.compile_manager(("-DPLB_ANDROID4G_CUSTOM_GUEST=1",))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Custom guest kernel requires", result.stderr)

    def test_fixed_copy_supervisor_compiles(self):
        result = subprocess.run(
            [
                str(COMPILER),
                "-fsyntax-only",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(ROOT / "tools/android4g_profile_copy.c"),
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_loader_bridge_requires_adopted_profile_and_capset_selection(self):
        flags = ("-DPLB_ANDROID4G_LOADER_COMPAT=1", "-DPLB_ANDROID4G_BUFFER_CAPSET=1")
        result = self.compile_manager(flags, adopted=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        for extra, adopted in ((flags, False), (flags[:1], True)):
            result = self.compile_manager(extra, adopted=adopted)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Loader compatibility requires", result.stderr)

    def test_fixed_payload_render_build_compiles_only_in_the_gpu_diagnostic(self):
        result = self.compile_manager(("-DPLB_ANDROID_GPU_RENDER_PROBE=1",))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.compile_manager(
            ("-DPLB_ANDROID_GPU_RENDER_PROBE=1", "-UPLB_ANDROID_GPU_MEMORY_PROBE")
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("guest render test is restricted", result.stderr)

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
