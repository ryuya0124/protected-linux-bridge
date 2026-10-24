"""Media evidence must prove publication, complete reads and no device write."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from native_xnu_media_result import assess


class NativeXnuMediaEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.nonce = "1" * 32
        self.firmware = "2" * 32
        self.contract = {"nonce": self.nonce, "firmware_nonce": self.firmware}
        self.pattern = {"nonce": self.nonce, "disk_bytes": 2097152, "disk_fnv64": 123}
        self.rows = [
            ("entry", 0, 0),
            ("candidate", 8, 131072),
            ("candidate", 16, 4096),
            ("queue_ready", 0x9FFF0000, 8192),
            ("media_published", 2097152, 0),
            ("bsd_node", 1, 0),
            ("media_read_pass", 2097152, 123),
            ("cross_page_pass", 512, 4096),
            ("write_refused", 514, 514),
            ("device_reset", 0, 0),
            ("pass", 0, 0),
            ("guest_shutdown", 0, 0),
        ]
        self.runner = f"PLB_NATIVE_XNU_RUNNER nonce={self.nonce} result=0\n"

    def console(self, rows=None):
        return (
            f"PLB_NATIVE_XNU nonce={self.firmware} phase=uefi_entry status=0\n"
            f"PLB_NATIVE_XNU nonce={self.firmware} phase=loader_ready status=0\n"
            "Darwin Kernel Version 23.6.0: test\nkprintf initialized\n"
            + "".join(
                f"PLB_XNU_MEDIA nonce={self.nonce} phase={p} a={a} b={b}\n"
                for p, a, b in (self.rows if rows is None else rows)
            )
        )

    def test_complete_media_does_not_claim_an_ios_boot(self):
        result = assess(self.console(), self.runner, self.contract, self.pattern)
        self.assertTrue(result["iomedia_readback_verified"])
        self.assertFalse(result["ios_boot_verified"])

    def test_partial_reads_writable_media_and_device_writes_are_rejected(self):
        for index, value in (
            (3, ("queue_ready", 0x84400000, 8192)),
            (4, ("media_published", 2097152, 1)),
            (5, ("bsd_node", 0, 0)),
            (6, ("media_read_pass", 4096, 123)),
            (6, ("media_read_pass", 2097152, 124)),
            (8, ("write_refused", 514, 515)),
        ):
            rows = list(self.rows)
            rows[index] = value
            with self.assertRaises(ValueError):
                assess(self.console(rows), self.runner, self.contract, self.pattern)

    def test_panic_partial_shutdown_and_stale_runner_are_rejected(self):
        for console, runner in (
            (self.console() + "panic(cpu 0 caller 0xffff0000): failure\n", self.runner),
            (self.console(self.rows[:-1]), self.runner),
            (self.console(), self.runner.replace(self.nonce, self.firmware)),
            (self.console(), self.runner.replace("result=0", "result=88")),
        ):
            with self.assertRaises(ValueError):
                assess(console, runner, self.contract, self.pattern)
