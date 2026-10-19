"""Exercise real runner budgets and the native Rust import contract; no VM/ADB."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PreallocatedVmLimitsTests(unittest.TestCase):
    def test_aligned_mapping_preserves_file_offsets_and_neighbors(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "alignment"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures/preallocated_alignment.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_full_content_detects_late_word_errors_and_page_aliases(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "pattern"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures/full_memory_pattern.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_only_a_valid_owned_write_pipe_is_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "ready"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures/preallocated_ready.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_exact_pairs_reject_unsafe_working_sets(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "limits"
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures/preallocated_vm_limits.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=3)

    @unittest.skipUnless(shutil.which("rustc"), "Rust compiler unavailable")
    def test_import_contract_rejects_invalid_descriptors(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "contract"
            subprocess.run(
                [
                    "rustc",
                    "--edition",
                    "2021",
                    "--test",
                    str(ROOT / "tools/crosvm_preallocated_ram.rs"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=3)

    @unittest.skipUnless(shutil.which("rustc"), "Rust compiler unavailable")
    def test_lend_chunks_preserve_every_byte(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "chunks"
            subprocess.run(
                [
                    "rustc",
                    "--edition",
                    "2021",
                    "--test",
                    str(ROOT / "tools/crosvm_split_lend.rs"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=3)


if __name__ == "__main__":
    unittest.main()
