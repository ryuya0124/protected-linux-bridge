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
