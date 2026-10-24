"""Refuse ambiguous or malformed guest CPU evidence before generating an XNU tree."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_native_xnu_adt import cpu_metadata


class NativeXnuMetadataTest(unittest.TestCase):
    def setUp(self):
        self.nonce = "0123456789abcdef" * 2
        self.row = f"PLB_NATIVE_UEFI_CPU nonce={self.nonce} cntfrq=19200000 mpidr=3221225472\n"

    def test_mpidr_control_bits_do_not_become_cpu_affinity(self):
        self.assertEqual(cpu_metadata(self.row, self.nonce), (19200000, 0))

    def test_missing_duplicate_and_wrong_trial_are_refused(self):
        for text in ["", self.row * 2, self.row.replace(self.nonce, "f" * 32)]:
            with self.assertRaises(ValueError):
                cpu_metadata(text, self.nonce)

    def test_invalid_register_width_or_zero_frequency_is_refused(self):
        for text in [
            self.row.replace("cntfrq=19200000", "cntfrq=0"),
            self.row.replace("cntfrq=19200000", "cntfrq=4294967296"),
            self.row.replace("mpidr=3221225472", "mpidr=18446744073709551616"),
        ]:
            with self.assertRaises(ValueError):
                cpu_metadata(text, self.nonce)
