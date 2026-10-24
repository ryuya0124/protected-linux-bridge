"""Reject partial reads, stale receipts and unsupported original-family claims."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from native_xnu_original_result import assess, APPOS_SHA256


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

    def test_small_official_apfs_fixture_requires_its_exact_pin_and_header(self):
        contract = dict(self.contract, fixture_kind="official_appos")
        pattern = dict(self.pattern, disk_bytes=10485760, disk_sha256=APPOS_SHA256)
        rows = list(self.rows)
        rows[3] = ("MEDIA", "candidate", 16, 20480)
        rows[7] = ("ORIGINAL", "bsd_read_pass", 10485760, 456)
        rows.insert(7, ("ORIGINAL", "apfs_container_header", 10485760, 4096))
        result = assess(self.console(rows), self.runner, contract, pattern)
        self.assertEqual(result["bytes_verified"], 10485760)
        self.assertTrue(result["apfs_container_header_verified"])
        self.assertFalse(result["filesystem_mount_verified"])
        with self.assertRaises(ValueError):
            assess(self.console(rows), self.runner, contract, dict(pattern, disk_sha256="0" * 64))
        rows.pop(7)
        with self.assertRaises(ValueError):
            assess(self.console(rows), self.runner, contract, pattern)

    def mount_rows(self, mount_error=0, auth_error=13):
        rows = list(self.rows[:9])
        rows[3] = ("MEDIA", "candidate", 16, 20480)
        rows[7] = ("ORIGINAL", "bsd_read_pass", 10485760, 456)
        rows += [
            ("ORIGINAL", "root_selected", 1, 7),
            ("ORIGINAL", "vfs_mount_result", mount_error, 0),
        ]
        if mount_error == 0:
            rows += [
                ("ORIGINAL", "vfs_apfs_readonly", 1, 1),
                ("ORIGINAL", "apple_root_auth_result", auth_error, 0),
            ]
        rows += [
            ("ORIGINAL", "device_reset", 0, 0),
            ("ORIGINAL", "mount_probe_complete", 0, 0),
            ("ORIGINAL", "guest_shutdown", 0, 0),
        ]
        rows.insert(7, ("ORIGINAL", "apfs_container_header", 10485760, 4096))
        return rows

    def test_real_mount_and_failed_authentication_are_distinct(self):
        contract = dict(self.contract, fixture_kind="official_appos", mount_probe=True)
        pattern = dict(self.pattern, disk_bytes=10485760, disk_sha256=APPOS_SHA256)
        result = assess(self.console(self.mount_rows()), self.runner, contract, pattern)
        self.assertTrue(result["filesystem_mount_verified"])
        self.assertTrue(result["root_authentication_evaluated"])
        self.assertEqual(result["root_authentication_errno"], 13)
        self.assertFalse(result["root_authentication_verified"])
        self.assertFalse(result["ios_boot_verified"])

    def test_failed_mount_does_not_invent_an_authentication_result(self):
        contract = dict(self.contract, fixture_kind="official_appos", mount_probe=True)
        pattern = dict(self.pattern, disk_bytes=10485760, disk_sha256=APPOS_SHA256)
        result = assess(
            self.console(self.mount_rows(mount_error=22)), self.runner, contract, pattern
        )
        self.assertFalse(result["filesystem_mount_verified"])
        self.assertFalse(result["root_authentication_evaluated"])
        self.assertIsNone(result["root_authentication_errno"])

    def test_missing_mount_or_auth_result_and_writable_mount_are_refused(self):
        contract = dict(self.contract, fixture_kind="official_appos", mount_probe=True)
        pattern = dict(self.pattern, disk_bytes=10485760, disk_sha256=APPOS_SHA256)
        for bad in ("vfs_mount_result", "apple_root_auth_result"):
            rows = [row for row in self.mount_rows() if row[1] != bad]
            with self.assertRaises(ValueError):
                assess(self.console(rows), self.runner, contract, pattern)
        rows = [
            (tag, phase, a, 0 if phase == "vfs_apfs_readonly" else b)
            for tag, phase, a, b in self.mount_rows()
        ]
        with self.assertRaises(ValueError):
            assess(self.console(rows), self.runner, contract, pattern)


if __name__ == "__main__":
    unittest.main()
