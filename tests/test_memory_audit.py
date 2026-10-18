"""Keep read-only diagnosis separate from backing guarantees and root-cause claims."""

import importlib.util
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "memory_audit", ROOT / "tools/audit_memory_backing.py"
)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class MemoryAuditTests(unittest.TestCase):
    def test_selected_policy_requires_brackets(self):
        self.assertEqual(
            AUDIT.selected_policy("always within_size advise [never] deny force"), "never"
        )
        self.assertEqual(
            AUDIT.selected_policy("always defer [defer+madvise] madvise never"), "defer+madvise"
        )
        self.assertIsNone(AUDIT.selected_policy("always advise never"))
        self.assertIsNone(AUDIT.selected_policy("[always] [never]"))

    def test_two_megabyte_order_depends_on_host_page_size(self):
        counts = "0 0 0 0 0 0 0 1 0 3 2"
        text = "Node 0, zone Normal " + counts
        self.assertEqual(AUDIT.buddy_summary(text, 4096)["free_2m_equivalents"], 7)
        self.assertEqual(AUDIT.buddy_summary(text, 16384)["free_2m_equivalents"], 29)
        self.assertIsNone(AUDIT.buddy_summary(text, 0))
        self.assertIsNone(AUDIT.buddy_summary("permission denied", 4096))

    def test_each_zone_counted_without_capacity_guarantee(self):
        text = "Node 0, zone DMA 0 0 0 0 0 0 0 0 0 1 0\n"
        text += "Node 1, zone Normal 0 0 0 0 0 0 0 0 0 2 0\n"
        result = AUDIT.buddy_summary(text, 4096)
        self.assertEqual(result["free_2m_equivalents"], 3)
        self.assertFalse(result["capacity_guaranteed"])

    def test_stage_is_not_derived_from_generic_enomem(self):
        result = AUDIT.classify_failure("Failed to provide folio for guest addr: hidden: -12")
        self.assertEqual(result["observed_stages"], ["folio_provision_enomem"])
        self.assertEqual(result["exact_exhausted_resource"], "unproven")
        result = AUDIT.classify_failure(
            "Failed to map guest address hidden: 10\nFailed to provide folio hidden: -12"
        )
        self.assertIn("gunyah_addrspace_map_nomem", result["observed_stages"])
        self.assertNotIn("gunyah_memextent_donate_nomem", result["observed_stages"])

    def test_missing_data_never_passes_headroom_or_policy(self):
        result = AUDIT.summarize({"reads": {}})
        self.assertIsNone(result["passes_existing_headroom_check"])
        self.assertIsNone(result["shmem_policy"])
        self.assertIsNone(result["boundary_unchanged"])
        self.assertFalse(result["huge_folios_verified_for_vm"])

    def test_four_gib_requires_host_headroom(self):
        result = AUDIT.summarize({"reads": {"meminfo": "MemAvailable: 2230020 kB"}})
        self.assertFalse(result["passes_existing_headroom_check"])
        enough = (4096 + 768) * 1024
        result = AUDIT.summarize({"reads": {"meminfo": f"MemAvailable: {enough} kB"}})
        self.assertTrue(result["passes_existing_headroom_check"])
        self.assertEqual(result["gunyah_internal_cause"], "unproven")

    def test_wrong_device_refuses_all_further_reads(self):
        calls = []

        def shell(command):
            calls.append(command)
            return "Pixel 9" if command == "getprop ro.product.model" else "0"

        with self.assertRaises(ValueError):
            AUDIT.collect(shell)
        self.assertEqual(len(calls), 2)

    def test_changed_or_unreadable_boundary_is_not_verified(self):
        reads = {key: "known" for key in AUDIT.BOUNDARY}
        reads["selinux"] = "Enforcing"
        snapshot = {"reads": reads, "boundary_after": dict(reads)}
        self.assertTrue(AUDIT.summarize(snapshot)["boundary_unchanged"])
        snapshot["boundary_after"]["shmem_thp"] = "changed"
        self.assertFalse(AUDIT.summarize(snapshot)["boundary_unchanged"])
        snapshot["boundary_after"]["shmem_thp"] = ""
        self.assertIsNone(AUDIT.summarize(snapshot)["boundary_unchanged"])

    def test_optional_missing_policy_preserved_as_unknown(self):
        def shell(command):
            if command == "getprop ro.product.model":
                return "SM-S948Q"
            if command == "id -u":
                return "0"
            if command == AUDIT.READS["shmem_thp"]:
                raise subprocess.CalledProcessError(1, command)
            return "unrelated text"

        snapshot = AUDIT.collect(shell)
        self.assertIn("shmem_thp", snapshot["errors"])
        self.assertIsNone(AUDIT.summarize(snapshot)["shmem_policy"])


if __name__ == "__main__":
    unittest.main()
