"""A loader, version banner or VMM exit must not be reported as a working iOS guest."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from native_xnu_result import assess


class NativeXnuResultTest(unittest.TestCase):
    def setUp(self):
        self.nonce = "0123456789abcdef" * 2
        self.console = (
            f"PLB_NATIVE_XNU nonce={self.nonce} phase=uefi_entry status=0\n"
            f"PLB_NATIVE_XNU nonce={self.nonce} phase=loader_ready status=0\n"
            "kprintf initialized\nDarwin Kernel Version 23.6.0: local source build\n"
            "VM bootstrap done: 39 maps\n"
            "panic(cpu 0 caller 0xfffffff0079a9d7c): allocator assertion failed\n"
        )
        self.runner = f"PLB_NATIVE_XNU_RUNNER nonce={self.nonce} result=124\n"

    def test_panic_is_early_execution_and_never_a_working_ios(self):
        result = assess(self.console, self.runner, self.nonce)
        self.assertTrue(result["xnu_serial_execution_observed"])
        self.assertTrue(result["vm_bootstrap_observed"])
        self.assertEqual(result["guest_panic"], "allocator assertion failed")
        self.assertTrue(result["diagnostic_deadline_reached"])
        self.assertFalse(result["ios_boot_verified"])
        self.assertFalse(result["normal_guest_shutdown_verified"])

    def test_successful_vmm_exit_without_kernel_output_is_not_execution(self):
        console = "\n".join(self.console.splitlines()[:2])
        result = assess(console, self.runner.replace("124", "0"), self.nonce)
        self.assertFalse(result["xnu_serial_execution_observed"])
        self.assertFalse(result["ios_boot_verified"])

    def test_host_health_failure_remains_visible(self):
        result = assess(self.console, self.runner.replace("124", "88"), self.nonce)
        self.assertTrue(result["runtime_safety_stop"])
        self.assertFalse(result["ios_boot_verified"])

    def test_root_wait_and_crypto_output_do_not_override_a_safety_stop(self):
        console = self.console.split("panic(cpu", 1)[0]
        console += (
            "FIPSPOST_KEXT: all tests PASSED\n"
            "corecrypto_kext_start completed successfully\n"
            "Skywalk: running self-tests\n"
            "Waiting on <dict><string>IOMedia</string><string>Apple_HFS</string></dict>\n"
        )
        result = assess(console, self.runner.replace("124", "88"), self.nonce)
        self.assertTrue(result["crypto_post_all_passed_observed"])
        self.assertTrue(result["bsd_network_init_observed"])
        self.assertTrue(result["waiting_for_root_media_observed"])
        self.assertTrue(result["runtime_safety_stop"])
        self.assertFalse(result["root_filesystem_boot_verified"])
        self.assertFalse(result["ios_boot_verified"])

    def test_mixed_trials_duplicates_and_returning_loader_are_rejected(self):
        for console, runner in [
            (self.console.replace(self.nonce, "f" * 32), self.runner),
            (self.console, self.runner * 2),
            (
                self.console + f"PLB_NATIVE_XNU nonce={self.nonce} phase=loader_returned status=1",
                self.runner,
            ),
            ("Darwin Kernel Version 23.6.0:\n", self.runner),
        ]:
            with self.assertRaises(ValueError):
                assess(console, runner, self.nonce)
