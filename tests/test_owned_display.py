"""Exercise real session policy on the host; never execute Android/GPU artifacts."""

from pathlib import Path
import subprocess
import tempfile
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_owned_display_vmm import transform

ROOT = Path(__file__).resolve().parents[1]


class OwnedDisplayTest(unittest.TestCase):
    def test_cpu_probe_requires_nonce_bound_readback_not_vmm_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "cpu-probe-result"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures/cpu_probe_result.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_native_authorization_and_out_of_order_surface_events(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "owned-display-gate"
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "native/owned_display"),
                    str(ROOT / "tests/fixtures/owned_display_gate.cpp"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_overlay_rejects_unknown_dependency_graph(self):
        with self.assertRaises(ValueError):
            transform(
                {
                    "Android.bp": "no upstream binary",
                    "src/main.rs": "",
                    "gpu_display/Android.bp": "",
                }
            )

    def test_overlay_keeps_upstream_rendering_and_uses_private_backend(self):
        files = {
            "Android.bp": 'rust_binary {\n    name: "crosvm",\n    crate_name: "crosvm",\n}\n\nrust_test {\n}\n',
            "src/main.rs": "fn main() { upstream_entry(); }\n",
            "gpu_display/Android.bp": 'static_libs: [\n                "libcrosvm_android_display_client",\n],\nshared_libs: [\n                "libbinder_ndk",\n],\n',
        }
        changed = transform(files)
        self.assertIn('name: "libplb_vmm_android17"', changed["Android.bp"])
        self.assertIn('crate_name: "crosvm"', changed["Android.bp"])
        self.assertIn("plb_crosvm_owned_display_main", changed["src/main.rs"])
        self.assertIn("upstream_entry();", changed["src/main.rs"])
        self.assertNotIn("libcrosvm_android_display_client", changed["gpu_display/Android.bp"])
        self.assertIn("libplb_owned_display_backend", changed["gpu_display/Android.bp"])
        self.assertEqual(set(changed), {"Android.bp", "src/main.rs", "gpu_display/Android.bp"})
