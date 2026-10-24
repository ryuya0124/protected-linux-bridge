"""Do not turn integrity checks/tamper refusal into Apple-authenticated iOS boot."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import test_native_xnu_original_result as fixtures
from native_xnu_verified_result import assess
from build_native_ios_block_policy import TABLE_SHA256, TABLE_BYTES, CHUNK_BYTES
from native_ios_root_contract import RAW_SHA256, RAW_BYTES


class VerifiedRootEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.OriginalFamilyEvidenceTest()
        self.fixture.setUp()
        self.contract, self.pattern, self.console = self.fixture.main_fixture()
        self.contract.update(
            block_verification=True, inject_block_fault=False, owner_root_policy_enabled=False
        )
        self.pattern.update(
            inject_block_fault=False,
            block_policy={
                "image_bytes": RAW_BYTES,
                "image_sha256": RAW_SHA256,
                "table_bytes": TABLE_BYTES,
                "table_sha256": TABLE_SHA256,
                "chunk_bytes": CHUNK_BYTES,
            },
        )

    def receipt(self, phase, a, b):
        return f"PLB_XNU_ORIGINAL nonce={self.fixture.nonce} " f"phase={phase} a={a} b={b}\n"

    def positive(self):
        console = self.console.replace(
            self.receipt("block_started", 0, 0),
            self.receipt("sha256_kat", 1, 256)
            + self.receipt("integrity_policy_ready", CHUNK_BYTES, TABLE_BYTES // 32)
            + self.receipt("block_started", 0, 0),
        )
        return console.replace(
            self.receipt("device_reset", 0, 0),
            self.receipt("integrity_stats", 70, 400)
            + self.receipt("integrity_failures", 0, 0)
            + self.receipt("device_reset", 0, 0),
        )

    def negative(self):
        rows = self.fixture.rows[:7]
        rows[3] = ("MEDIA", "candidate", 16, RAW_BYTES // 512)
        rows = (
            rows[:5]
            + [
                ("ORIGINAL", "sha256_kat", 1, 256),
                ("ORIGINAL", "integrity_policy_ready", CHUNK_BYTES, TABLE_BYTES // 32),
            ]
            + rows[5:]
            + [
                ("ORIGINAL", "integrity_fault_injected", 0, 1),
                ("ORIGINAL", "integrity_rejected", 0, 0),
                ("ORIGINAL", "media_missing_or_failed", 0, 0),
                ("ORIGINAL", "integrity_stats", 0, 0),
                ("ORIGINAL", "integrity_failures", 1, 1),
                ("ORIGINAL", "device_reset", 0, 0),
                ("ORIGINAL", "failed", 0, 0),
                ("ORIGINAL", "guest_shutdown", 0, 0),
            ]
        )
        return self.fixture.console(rows)

    def test_verified_reads_keep_apple_failure_and_coverage_separate(self):
        result = assess(self.positive(), self.fixture.runner, self.contract, self.pattern)
        self.assertTrue(result["filesystem_mount_verified"])
        self.assertTrue(result["consumed_read_verification_observed"])
        self.assertEqual(result["authenticated_page_deliveries"], 400)
        self.assertFalse(result["root_authentication_verified"])
        self.assertFalse(result["owner_root_policy_enabled"])
        self.assertFalse(result["counts_are_unique_coverage"])
        self.assertFalse(result["ios_boot_verified"])

    def test_missing_duplicate_poisoned_or_bad_anchor_evidence_is_refused(self):
        good = self.positive()
        for bad in (
            good.replace("phase=sha256_kat a=1", "phase=sha256_kat a=0"),
            good.replace("phase=integrity_failures a=0", "phase=integrity_failures a=1"),
            good.replace("phase=integrity_stats a=70 b=400", "phase=integrity_stats a=401 b=400"),
            good + self.receipt("integrity_stats", 70, 400),
            good.replace("phase=integrity_policy_ready", "phase=missing_policy"),
        ):
            with self.assertRaises(ValueError):
                assess(bad, self.fixture.runner, self.contract, self.pattern)
        with self.assertRaises(ValueError):
            assess(
                good,
                self.fixture.runner,
                dict(self.contract, owner_root_policy_enabled=True),
                self.pattern,
            )
        changed = dict(
            self.pattern, block_policy=dict(self.pattern["block_policy"], table_sha256="0" * 64)
        )
        with self.assertRaises(ValueError):
            assess(good, self.fixture.runner, self.contract, changed)

    def test_negative_control_requires_actual_refusal_with_no_delivered_pages(self):
        contract = dict(self.contract, inject_block_fault=True)
        pattern = dict(self.pattern, inject_block_fault=True)
        result = assess(self.negative(), self.fixture.runner, contract, pattern)
        self.assertTrue(result["tamper_refusal_observed"])
        self.assertFalse(result["filesystem_mount_verified"])
        self.assertFalse(result["root_authentication_evaluated"])
        for bad in (
            self.negative().replace(
                "phase=integrity_rejected a=0 b=0", "phase=integrity_rejected a=0 b=1"
            ),
            self.negative().replace(
                "phase=integrity_failures a=1 b=1", "phase=integrity_failures a=1 b=0"
            ),
            self.negative() + self.receipt("root_selected", 1, 13),
            self.negative().replace("phase=queue_ready", "phase=queue_failed"),
        ):
            with self.assertRaises(ValueError):
                assess(bad, self.fixture.runner, contract, pattern)
