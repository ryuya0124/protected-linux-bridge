"""Partial DMA reads, private buffers and stale trial records never qualify."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from native_xnu_pci_result import assess


class NativeXnuPciEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.nonce = "0123456789abcdef" * 2
        self.contract = {"nonce": self.nonce}
        self.pci = {"nonce": self.nonce, "disk_bytes": 2097152, "disk_fnv64": 12345}
        self.kernel = (
            f"PLB_NATIVE_XNU nonce={self.nonce} phase=uefi_entry status=0\n"
            f"PLB_NATIVE_XNU nonce={self.nonce} phase=loader_ready status=0\n"
            "kprintf initialized\nDarwin Kernel Version 23.6.0: test\n"
        )
        self.rows = [
            ("entry", 0, 0),
            ("candidate", 8, 131072),
            ("candidate", 16, 4096),
            ("queue_ready", 0x9FFF0000, 8192),
            ("read_pass", 2097152, 12345),
            ("pass", 0, 0),
            ("guest_shutdown", 0, 0),
        ]
        self.runner = f"PLB_NATIVE_XNU_RUNNER nonce={self.nonce} result=0\n"

    def console(self, rows=None):
        return self.kernel + "".join(
            f"PLB_XNU_PCI nonce={self.nonce} phase={p} a={a} b={b}\n"
            for p, a, b in (rows or self.rows)
        )

    def test_complete_readback_is_not_ios(self):
        result = assess(self.console(), self.runner, self.contract, self.pci)
        self.assertTrue(result["xnu_pci_readback_verified"])
        self.assertFalse(result["ios_boot_verified"])

    def test_reused_firmware_keeps_a_distinct_fresh_runner_identity(self):
        firmware = "f" * 32
        kernel = self.kernel.replace(self.nonce, firmware)
        console = self.console().replace(self.kernel, kernel)
        contract = {"nonce": self.nonce, "firmware_nonce": firmware}
        self.assertTrue(
            assess(console, self.runner, contract, self.pci)["xnu_pci_readback_verified"]
        )
        with self.assertRaises(ValueError):
            assess(console, self.runner.replace(self.nonce, firmware), contract, self.pci)

    def test_guest_panic_does_not_qualify_even_with_success_receipts(self):
        console = self.console() + "panic(cpu 0 caller 0xffff0000): invalid mapping\n"
        with self.assertRaises(ValueError):
            assess(console, self.runner, self.contract, self.pci)

    def test_partial_or_wrong_data_and_private_dma_are_refused(self):
        for index, value in [
            (3, ("queue_ready", 0x84404000, 8192)),
            (4, ("read_pass", 4096, 12345)),
            (4, ("read_pass", 2097152, 1)),
        ]:
            rows = list(self.rows)
            rows[index] = value
            with self.assertRaises(ValueError):
                assess(self.console(rows), self.runner, self.contract, self.pci)
        with self.assertRaises(ValueError):
            assess(self.console(self.rows[:-1]), self.runner, self.contract, self.pci)

    def test_safety_stop_wrong_trial_and_duplicate_devices_are_refused(self):
        with self.assertRaises(ValueError):
            assess(
                self.console(),
                self.runner.replace("result=0", "result=88"),
                self.contract,
                self.pci,
            )
        with self.assertRaises(ValueError):
            assess(
                self.console().replace(self.nonce, "f" * 32), self.runner, self.contract, self.pci
            )
        rows = list(self.rows)
        rows[2] = ("candidate", 8, 131072)
        with self.assertRaises(ValueError):
            assess(self.console(rows), self.runner, self.contract, self.pci)
