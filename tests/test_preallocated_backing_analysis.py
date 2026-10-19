"""Offline comparisons must not turn incomplete/repeated samples into success."""

import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "backing_analysis", ROOT / "tools/analyze_preallocated_backing.py"
)
ANALYSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYSIS)


def batch(offset, thp=4096, heads=1024, sampled=4096):
    return (
        f"PLB_PREALLOCATED_BATCH offset_mib={offset} bytes=16777216\n"
        f"PLB_PREALLOCATED_PAGES sampled={sampled} thp={thp} "
        f"compound_heads={heads} expected=4096\n"
    )


class PreallocatedBackingAnalysisTest(unittest.TestCase):
    def test_reverse_order_and_full_thp_are_not_stability_validation(self):
        text = batch(16) + batch(0) + "PLB_PREALLOCATED_READY memory_mib=32\n"
        report = ANALYSIS.analyze(text)
        self.assertTrue(report["complete_preparation_snapshot"])
        self.assertEqual(report["thp_coverage_percent"], 100)
        self.assertEqual(report["thp_kib_per_observed_head"], 16)
        self.assertFalse(report["stability_validated_by_this_analysis"])
        self.assertIsNone(report["gunyah_mapping_count"])

    def test_pin_snapshot_is_not_counted_as_extra_ram(self):
        second = "PLB_PREALLOCATED_PAGES sampled=4096 thp=4096 compound_heads=128 expected=4096\n"
        report = ANALYSIS.analyze(batch(0) + second + "PLB_PREALLOCATED_READY memory_mib=16\n")
        self.assertEqual(report["sampled_pages"], 4096)
        self.assertEqual(report["snapshots_seen"], 2)
        self.assertEqual(report["compound_head_observations"], 128)

    def test_missing_extent_and_partial_observation_are_incomplete(self):
        for text in (
            batch(16) + "PLB_PREALLOCATED_READY memory_mib=32\n",
            batch(0, thp=1024, heads=256, sampled=1024) + "PLB_PREALLOCATED_READY memory_mib=16\n",
        ):
            self.assertFalse(ANALYSIS.analyze(text)["complete_preparation_snapshot"])

    def test_merged_trials_and_inconsistent_counters_are_refused(self):
        ready = "PLB_PREALLOCATED_READY memory_mib=16\n"
        for text in (
            batch(0) + ready + ready,
            batch(0) + batch(0) + ready,
            batch(0, thp=4097) + ready,
        ):
            with self.assertRaises(ValueError):
                ANALYSIS.analyze(text)
