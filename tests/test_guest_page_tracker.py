"""Guest PFN reuse counts must not include aliases, hidden or swapped entries."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GuestPageTrackerTests(unittest.TestCase):
    def test_guest_bitmap_decoding_and_bounds(self):
        self.compile_and_run("guest_page_tracker.c")

    def test_owned_backing_order_is_not_folio_size(self):
        self.compile_and_run("backing_order_summary.c")

    def test_middle_forward_and_reverse_preserve_both_margins(self):
        self.compile_and_run("backing_page_order.c")

    def test_known_host_page_or_oops_failures_refuse_new_load(self):
        self.compile_and_run("host_memory_health.c")

    def test_retired_refault_preserves_supplied_mapping_and_file(self):
        self.compile_and_run("retired_shmem_refault.c")

    def compile_and_run(self, fixture):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "guest-page-tracker"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools"),
                    str(ROOT / "tests/fixtures" / fixture),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)
