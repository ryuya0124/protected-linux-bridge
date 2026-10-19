"""Offline fixtures prevent confusing error domains or repeating kernel snapshots."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from audit_gunyah_logs import audit, read_log, MAX_BYTES


class GunyahLogAuditTest(unittest.TestCase):
    def test_mapping_nomem_does_not_identify_a_host_allocation_or_exact_heap(self):
        result = audit(
            "[1.0] Failed to map guest address 0x12340000: 10\n"
            "[1.1] Failed to provide folio for guest addr: 000012340000: -12\n"
            "vcpu hit unknown error: Out of memory (os error 12)\n"
        )
        self.assertTrue(result["address_map_nomem_observed"])
        self.assertEqual(result["errors_by_stage_and_domain"]["provide_folio_linux"], {"-12": 1})
        self.assertFalse(result["exact_hypervisor_allocator_identified"])
        self.assertFalse(result["vm_success_inferred_from_exit_status"])

    def test_donation_and_rm_error_numbers_stay_in_their_own_domains(self):
        result = audit(
            "Failed to donate memory for guest address 0x12340000: 111\n"
            "RM rejected message 51000015. Error: 7\n"
        )
        self.assertFalse(result["address_map_nomem_observed"])
        self.assertEqual(result["errors_by_stage_and_domain"]["donate_hypervisor"], {"111": 1})
        self.assertEqual(result["errors_by_stage_and_domain"]["resource_manager"], {"7": 1})

    def test_prefault_refusal_does_not_become_a_hypervisor_fault(self):
        result = audit("ANDROID_BACKING_UNSUITABLE\nANDROID_GPU_VMM_STATUS raw=22784 signal=0\n")
        self.assertEqual(result["prefault_admission_refusal_lines"], 1)
        self.assertFalse(result["address_map_nomem_observed"])

    def test_repeated_timestamped_records_are_not_new_faults(self):
        record = "[100.125] BUG: Bad page cache in process fixture pfn:deadbeef\n"
        result = audit(record + record + "[101.5] BUG: Bad page cache pfn:cafe\n")
        self.assertEqual(result["distinct_timed_bad_page_records"], 2)
        self.assertEqual(len(result["bad_page_bursts"]), 2)
        self.assertNotIn("deadbeef", json.dumps(result))
        self.assertNotIn("fixture", json.dumps(result))

    def test_untimed_repeated_lines_are_not_silently_deduplicated(self):
        result = audit("BUG: Bad page cache\nBUG: Bad page cache\n")
        self.assertEqual(result["untimed_bad_page_lines"], 2)
        self.assertEqual(result["distinct_timed_bad_page_records"], 0)

    def test_reclaim_failure_and_linux_oom_are_separate(self):
        result = audit(
            "Failed to reclaim memory donation for guest address 0x1234: 111\n"
            "Failed to unmap guest address 0x1234: 10\n"
            "Gunyah failed to reclaim gunyah_address_range\n"
            "Out of memory and no killable processes\n"
        )
        self.assertFalse(result["address_map_nomem_observed"])
        self.assertEqual(result["errors_by_stage_and_domain"]["unmap_hypervisor"], {"10": 1})
        self.assertEqual(result["linux_oom_lines_scope_unspecified"], 1)

    def test_reader_rejects_symlink_and_sparse_oversized_input(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "saved.log"
            path.write_bytes(b"ANDROID_BACKING_UNSUITABLE\n")
            self.assertIn("UNSUITABLE", read_log(path))
            link = Path(folder) / "link"
            link.symlink_to(path)
            with self.assertRaises(OSError):
                read_log(link)
            with path.open("wb") as stream:
                stream.truncate(MAX_BYTES + 1)
            with self.assertRaises(ValueError):
                read_log(path)
