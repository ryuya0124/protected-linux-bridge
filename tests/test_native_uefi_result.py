"""Reject partial reads, wrong trial identities, guest failures and host-side aborts."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from native_uefi_result import assess


class NativeUefiEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.nonce = "0123456789abcdef" * 2
        self.contract = {"nonce": self.nonce, "disk_bytes": 2097152, "disk_fnv64": 987654321}
        self.lines = [
            self.line("entry", 1, 0),
            self.line("dma_window", 0x9E000000, 33554432),
            self.line("read_pass", 2097152, 987654321),
            self.line("pass", 0, 0),
            self.line("guest_shutdown", 0, 0),
        ]
        self.runner = f"PLB_NATIVE_UEFI_RUNNER nonce={self.nonce} result=0\n"

    def line(self, phase, first, second):
        return f"PLB_NATIVE_UEFI nonce={self.nonce} phase={phase} a={first} b={second}\n"

    def test_complete_native_probe_is_not_an_ios_boot(self):
        result = assess("".join(self.lines), self.runner, self.contract)
        self.assertEqual(result["synthetic_disk_bytes_verified"], 2097152)
        self.assertFalse(result["ios_boot_verified"])

    def test_partial_read_wrong_digest_and_missing_shutdown(self):
        for phase in [self.line("read_pass", 4096, 987654321), self.line("read_pass", 2097152, 1)]:
            lines = list(self.lines)
            lines[2] = phase
            with self.assertRaises(ValueError):
                assess("".join(lines), self.runner, self.contract)
        with self.assertRaises(ValueError):
            assess("".join(self.lines[:-1]), self.runner, self.contract)

    def test_wrong_nonce_el_and_dma_window(self):
        with self.assertRaises(ValueError):
            assess("".join(self.lines).replace(self.nonce, "f" * 32), self.runner, self.contract)
        for index, value in [
            (0, self.line("entry", 2, 0)),
            (1, self.line("dma_window", 0x80000000, 536870912)),
            (1, self.line("dma_window", 0xA0000000, 33554432)),
        ]:
            lines = list(self.lines)
            lines[index] = value
            with self.assertRaises(ValueError):
                assess("".join(lines), self.runner, self.contract)

    def test_guest_failure_and_host_abort_are_not_success(self):
        with self.assertRaises(ValueError):
            assess(
                "".join(self.lines).replace("phase=pass", "phase=failed"),
                self.runner,
                self.contract,
            )
        for runner in ["", self.runner.replace("result=0", "result=88"), self.runner * 2]:
            with self.assertRaises(ValueError):
                assess("".join(self.lines), runner, self.contract)
