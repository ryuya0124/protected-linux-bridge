"""A VMM exit status is not proof that a guest completed its workload."""

import importlib.util
from pathlib import Path
import unittest
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("memory_probe", ROOT / "tools/run_memory_probe.py")
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class MemoryProbeResult(unittest.TestCase):
    def setUp(self):
        self.success = (
            "PLB_MEMORY_BEGIN mib=256 guest_page=4096\r\n"
            "PLB_MEMORY_TOUCHED mib=256\r\n"
            "PLB_MEMORY_READBACK errors=0\r\n"
            "PLB_MEMORY_COMPLETE\r\n"
        )

    def test_readback_then_completion(self):
        self.assertTrue(
            PROBE.assess_probe(0, self.success, "exiting with success", 256)["complete"]
        )

    def repeated_log(self, rounds):
        return (
            "".join(
                f"PLB_MEMORY_ROUND round={value} errors=0 minor_faults=98304 "
                "major_faults=0 maxrss_kib=393216\n"
                for value in rounds
            )
            + self.success
        )

    def test_all_repeated_rounds_required(self):
        self.assertTrue(
            PROBE.assess_probe(0, self.repeated_log(range(1, 9)), "", 256, 8)["complete"]
        )
        for rounds in ([1, 2], [2, 1, 3], [1, 2, 2, 3], []):
            self.assertFalse(
                PROBE.assess_probe(0, self.repeated_log(rounds), "", 256, 3)["complete"]
            )

    def test_error_in_an_earlier_round_is_failure(self):
        log = self.repeated_log([1, 2]).replace("round=1 errors=0", "round=1 errors=1")
        self.assertFalse(PROBE.assess_probe(0, log, "", 256, 2)["complete"])

    def test_extra_round_is_not_the_requested_workload(self):
        self.assertFalse(PROBE.assess_probe(0, self.repeated_log([1, 2]), "", 256, 1)["complete"])

    def test_observed_zero_exit_vcpu_crash(self):
        guest = "PLB_MEMORY_TOUCHED mib=448\n"
        vmm = "unknown gh exit reason: 3\nvcpu crashed\nexiting with success\n"
        result = PROBE.assess_probe(0, guest, vmm, 768)
        self.assertFalse(result["complete"])
        self.assertTrue(result["vcpu_failure"])

    def test_fault_after_completion_is_failure(self):
        self.assertFalse(PROBE.assess_probe(0, self.success, "vcpu crashed", 256)["complete"])

    def test_marker_without_readback_is_failure(self):
        self.assertFalse(PROBE.assess_probe(0, "PLB_MEMORY_COMPLETE\n", "", 256)["complete"])

    def test_readback_error_is_failure(self):
        guest = self.success.replace("errors=0", "errors=1")
        self.assertFalse(PROBE.assess_probe(0, guest, "", 256)["complete"])

    def test_timeout_is_failure_even_with_marker(self):
        self.assertFalse(PROBE.assess_probe(124, self.success, "", 256)["complete"])

    def test_wrong_requested_size_is_failure(self):
        self.assertFalse(PROBE.assess_probe(0, self.success, "", 512)["complete"])

    def test_completion_before_readback_is_failure(self):
        guest = "PLB_MEMORY_COMPLETE\nPLB_MEMORY_TOUCHED mib=256\nPLB_MEMORY_READBACK errors=0\n"
        self.assertFalse(PROBE.assess_probe(0, guest, "", 256)["complete"])


class WorkloadBoundsTest(unittest.TestCase):
    def test_large_reservation_is_not_a_large_resident_workload(self):
        self.assertEqual(PROBE.workload_budget_mib(4096, 64, 1), 576)
        self.assertEqual(PROBE.workload_budget_mib(4096, 256, 1), 768)
        for keywords in ({"private_backing": True}, {"hugepages": True}):
            with self.assertRaises(ValueError):
                PROBE.workload_budget_mib(4096, 64, 1, **keywords)
        with self.assertRaises(ValueError):
            PROBE.workload_budget_mib(4096, 512, 1)

    def test_repeated_workload_fits_watchdog(self):
        self.assertEqual(PROBE.workload_budget_mib(768, 384, 8), 768)
        for memory, touch, rounds in ((1024, 768, 8), (768, 384, 9), (512, 500, 1)):
            with self.assertRaises(ValueError):
                PROBE.workload_budget_mib(memory, touch, rounds)


class CapturedFdtTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("dtc"), "Requires the independent device-tree compiler")
    def test_real_dtc_output_and_damaged_captures(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "layout.dtb"
            subprocess.run(
                [
                    "dtc",
                    "-I",
                    "dts",
                    "-O",
                    "dtb",
                    "-o",
                    str(target),
                    str(ROOT / "tests/fixtures/memory-layout.dts"),
                ],
                check=True,
            )
            data = target.read_bytes()
        self.assertTrue(PROBE.valid_fdt_dump(data))
        self.assertFalse(PROBE.valid_fdt_dump(data[:-1]))
        self.assertFalse(PROBE.valid_fdt_dump(data + b"unexpected transport output"))
        damaged = bytearray(data)
        struct.pack_into(">I", damaged, 8, len(data) + 1024)
        self.assertFalse(PROBE.valid_fdt_dump(damaged))
        damaged = bytearray(data)
        struct.pack_into(">I", damaged, 36, len(data))
        self.assertFalse(PROBE.valid_fdt_dump(damaged))
        self.assertFalse(PROBE.valid_fdt_dump(b"root request denied"))


if __name__ == "__main__":
    unittest.main()
