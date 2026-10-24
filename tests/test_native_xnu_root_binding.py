"""Mounted-root audit must fail closed for identity, provider and evidence changes."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import test_native_xnu_verified_result as fixtures
from native_xnu_binding_result import assess
from native_xnu_verified_result import assess as assess_reads


class RootBindingAuditTest(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.VerifiedRootEvidenceTest()
        self.fixture.setUp()
        self.contract = dict(self.fixture.contract, root_binding_audit=True)
        self.pattern = dict(self.fixture.pattern, root_binding_audit=True)

    def receipt(self, phase, a, b):
        return self.fixture.receipt(phase, a, b)

    def console(self):
        audit = (
            self.receipt("root_binding_device", 1, 1)
            + self.receipt("root_binding_matches", 1, 9)
            + self.receipt("root_binding_checks", 127, 127)
            + self.receipt("root_binding_negative", 7, 7)
            + self.receipt("root_binding_audit", 1, 0)
        )
        console = self.fixture.positive().replace(
            self.receipt("apple_root_auth_result", 13, 0),
            self.receipt("apple_root_auth_result", 80, 0),
        )
        return console.replace(
            self.receipt("integrity_stats", 70, 400),
            audit + self.receipt("integrity_stats", 70, 400),
        )

    def test_audit_preserves_actual_apple_refusal_and_does_not_accept_root(self):
        result = assess(self.console(), self.fixture.fixture.runner, self.contract, self.pattern)
        self.assertTrue(result["mounted_root_binding_audit_verified"])
        self.assertTrue(result["root_binding_negative_controls_verified"])
        self.assertFalse(result["owner_root_policy_enabled"])
        self.assertFalse(result["root_authentication_verified"])
        self.assertFalse(result["ios_boot_verified"])
        with self.assertRaises(ValueError):
            assess_reads(self.console(), self.fixture.fixture.runner, self.contract, self.pattern)

    def test_missing_duplicate_moved_or_failed_binding_controls_are_refused(self):
        good = self.console()
        for bad in (
            good.replace("phase=root_binding_checks a=127", "phase=root_binding_checks a=63"),
            good.replace("phase=root_binding_matches a=1", "phase=root_binding_matches a=2"),
            good.replace("phase=root_binding_negative a=7", "phase=root_binding_negative a=3"),
            good.replace("phase=root_binding_audit a=1 b=0", "phase=root_binding_audit a=1 b=1"),
            good.replace(self.receipt("root_binding_device", 1, 1), ""),
            good.replace("phase=apple_root_auth_result a=80", "phase=apple_root_auth_result a=0"),
            good + self.receipt("root_binding_audit", 1, 0),
            self.receipt("root_binding_audit", 1, 0) + good,
            good.replace(
                self.receipt("root_binding_device", 1, 1),
                self.receipt("root_binding_device", 1, 1).replace(self.contract["nonce"], "f" * 32),
            ),
        ):
            with self.assertRaises(ValueError):
                assess(bad, self.fixture.fixture.runner, self.contract, self.pattern)
        with self.assertRaises(ValueError):
            assess(
                good,
                self.fixture.fixture.runner,
                dict(self.contract, root_binding_audit=False),
                self.pattern,
            )

    def test_real_shared_provider_and_policy_predicates_under_sanitizers(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "root-binding"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-O1",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    str(ROOT / "tests/fixtures/root_binding.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            result = subprocess.run(
                [str(binary)], capture_output=True, text=True, check=True, timeout=10
            )
            self.assertIn("bounded mounted-root audit controls passed", result.stdout)
