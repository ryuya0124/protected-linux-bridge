"""Reject partial reads, stale receipts and unsupported original-family claims."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from native_xnu_original_result import assess


class OriginalFamilyEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.nonce = "3" * 32
        self.firmware = "4" * 32
        self.contract = {
            "nonce": self.nonce,
            "firmware_nonce": self.firmware,
            "source_storage_archive_linked": False,
        }
        self.pattern = {"nonce": self.nonce, "disk_bytes": 2097152, "disk_fnv64": 456}
        self.runner = f"PLB_NATIVE_XNU_RUNNER nonce={self.nonce} result=0\n"
        self.rows = [
            ("ORIGINAL", "entry", 0, 0),
            ("ORIGINAL", "base_class_size", 144, 144),
            ("MEDIA", "candidate", 8, 131072),
            ("MEDIA", "candidate", 16, 4096),
            ("MEDIA", "queue_ready", 0x9FFF0000, 8192),
            ("ORIGINAL", "block_started", 0, 0),
            ("ORIGINAL", "bsd_node", 1, 0),
            ("ORIGINAL", "bsd_read_pass", 2097152, 456),
            ("ORIGINAL", "bsd_closed", 0, 0),
            ("ORIGINAL", "device_reset", 0, 0),
            ("ORIGINAL", "pass", 0, 0),
            ("ORIGINAL", "guest_shutdown", 0, 0),
        ]

    def console(self, rows=None):
        return (
            f"PLB_NATIVE_XNU nonce={self.firmware} phase=uefi_entry status=0\n"
            f"PLB_NATIVE_XNU nonce={self.firmware} phase=loader_ready status=0\n"
            "Darwin Kernel Version 23.6.0: fixture\nkprintf initialized\n"
            "FIPSPOST_KEXT fixture: all tests PASSED\n"
            "corecrypto_kext_start completed successfully\n"
            "apfs_module_start:3232: load: com.apple.filesystems.apfs, v2236.140.6, fixture\n"
            "apfs_sysctl_register:1407: done registering sysctls.\n"
            + "".join(
                f"PLB_XNU_{tag} nonce={self.nonce} phase={phase} a={a} b={b}\n"
                for tag, phase, a, b in (self.rows if rows is None else rows)
            )
        )

    def test_full_bsd_path_is_not_a_filesystem_boot(self):
        result = assess(self.console(), self.runner, self.contract, self.pattern)
        self.assertTrue(result["original_storage_family_bsd_readback_verified"])
        self.assertTrue(result["apfs_initialization_observed"])
        self.assertFalse(result["filesystem_mount_verified"])
        self.assertFalse(result["ios_boot_verified"])

    def test_base_size_dma_and_partial_or_wrong_read_are_refused(self):
        for index, replacement in (
            (1, ("ORIGINAL", "base_class_size", 152, 144)),
            (4, ("MEDIA", "queue_ready", 0x84400000, 8192)),
            (7, ("ORIGINAL", "bsd_read_pass", 4096, 456)),
            (7, ("ORIGINAL", "bsd_read_pass", 2097152, 457)),
        ):
            rows = list(self.rows)
            rows[index] = replacement
            with self.assertRaises(ValueError):
                assess(self.console(rows), self.runner, self.contract, self.pattern)

    def test_failed_or_ambiguous_guest_is_refused(self):
        for console, runner in (
            (self.console(self.rows[:-1]), self.runner),
            (self.console(self.rows + [self.rows[-1]]), self.runner),
            (self.console() + "panic(cpu 0 caller 0xffff0000): fixture\n", self.runner),
            (self.console().replace("all tests PASSED", "failed"), self.runner),
            (self.console(), self.runner.replace("result=0", "result=88")),
            (self.console(), self.runner.replace(self.nonce, self.firmware)),
        ):
            with self.assertRaises(ValueError):
                assess(console, runner, self.contract, self.pattern)

    def test_source_family_is_not_promoted_to_original_family(self):
        with self.assertRaises(ValueError):
            assess(
                self.console(),
                self.runner,
                dict(self.contract, source_storage_archive_linked=True),
                self.pattern,
            )

    def test_missing_apfs_start_does_not_invent_filesystem_initialization(self):
        result = assess(
            self.console().replace("apfs_module_start:", "not_a_module_start:"),
            self.runner,
            self.contract,
            self.pattern,
        )
        self.assertFalse(result["apfs_initialization_observed"])


if __name__ == "__main__":
    unittest.main()
