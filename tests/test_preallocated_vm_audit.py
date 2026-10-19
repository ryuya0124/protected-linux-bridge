"""A zero VMM exit after a page fault must never become a passing RAM result."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from audit_preallocated_vm import assess

GOOD = """PLB_RAM_ADOPT bytes=536870912 identity=1 seals=7 imported=1
PLB_MEMORY_PROGRESS round=1 mib=256
PLB_MEMORY_TOUCHED mib=256
PLB_MEMORY_READBACK errors=0
PLB_MEMORY_COMPLETE
PLB_PREALLOCATED_EXIT code=0
"""


class PreallocatedVmAuditTests(unittest.TestCase):
    def test_full_content_requires_the_exact_byte_count_without_errors(self):
        self.assertFalse(assess(GOOD, 512, 256, full_content=True)["full_readback_complete"])
        marker = "PLB_MEMORY_FULL_CONTENT checked_bytes=268435456 errors=0\n"
        result = assess(GOOD + marker, 512, 256, full_content=True)
        self.assertTrue(result["full_readback_complete"])
        self.assertTrue(result["all_bytes_verified"])
        for bad in (
            marker.replace("456", "448"),
            marker.replace("errors=0", "errors=1"),
            marker * 2,
        ):
            self.assertFalse(
                assess(GOOD + bad, 512, 256, full_content=True)["full_readback_complete"]
            )

    def test_positive_guest_completion_and_exact_object_are_required(self):
        self.assertTrue(assess(GOOD, 512, 256)["full_readback_complete"])
        self.assertFalse(assess(GOOD, 1024, 256)["full_readback_complete"])
        self.assertFalse(
            assess(GOOD.replace("seals=7", "seals=15"), 512, 256)["full_readback_complete"]
        )
        self.assertFalse(
            assess(GOOD.replace("PLB_MEMORY_COMPLETE", ""), 512, 256)["full_readback_complete"]
        )

    def test_zero_exit_after_fault_is_a_failure(self):
        data = GOOD + "PLB_GUNYAH_PAGE_FAULT attempt=-12 guest_addr=PRIVATE\nvcpu crashed\n"
        result = assess(data, 512, 256)
        self.assertFalse(result["full_readback_complete"])
        self.assertIsNone(result["verified_readback_mib"])
        self.assertEqual(result["linux_page_fault_attempt_errors"], [-12])
        self.assertNotIn("PRIVATE", str(result))

    def test_both_policy_restorers_are_required(self):
        data = GOOD + "PLB_POLICY_RESTORED=1 HOST_BOUNDARY_UNCHANGED=1\n"
        self.assertFalse(assess(data, 512, 256, True)["full_readback_complete"])
        data += "PLB_POLICY_GUARDIAN_RESTORED=1\n"
        self.assertTrue(assess(data, 512, 256, True)["full_readback_complete"])
        self.assertFalse(
            assess(data + "PLB_POLICY_GUARDIAN_RESTORED=0\n", 512, 256, True)[
                "full_readback_complete"
            ]
        )

    def test_outer_failure_late_boundary_change_and_unknown_seals_refuse(self):
        for data in (
            GOOD + "PLB_REMOTE_EXIT 35\n",
            GOOD + "PLB_REMOTE_EXIT 0\nPLB_REMOTE_EXIT 0\n",
            GOOD + "HOST_BOUNDARY_UNCHANGED=0\n",
            GOOD.replace("seals=7", "seals=71"),
        ):
            self.assertFalse(assess(data, 512, 256)["full_readback_complete"])

    def test_pin_snapshots_are_not_double_counted(self):
        data = GOOD + """PLB_PREALLOCATED_BATCH offset_mib=0 bytes=536870912
PLB_PREALLOCATED_PAGES sampled=131072 thp=0 compound_heads=0 expected=131072
PLB_PREALLOCATED_PAGES sampled=131072 thp=4096 compound_heads=256 expected=131072
"""
        result = assess(data, 512, 256)
        self.assertEqual(result["last_backing_sampled_pages"], 131072)
        self.assertEqual(result["last_backing_thp_pages"], 4096)


if __name__ == "__main__":
    unittest.main()
