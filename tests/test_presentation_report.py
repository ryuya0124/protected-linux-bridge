"""Presentation fences, lost history and duplicate callbacks need different results."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from presentation_report import analyze, parse_latency, PENDING

LAYER = "PLB owned display#1"
STEP = 8_333_333


def snapshot(frames):
    return {
        "layer": LAYER,
        "output": str(STEP) + "\n" + "".join(f"1 {frame * STEP} 2\n" for frame in frames),
    }


class PresentationReportTest(unittest.TestCase):
    def test_overlapping_history_counts_actual_presentations_after_anchor(self):
        result = analyze([snapshot(range(1, 128)), snapshot(range(61, 188))], LAYER)
        self.assertEqual(result["unique_presented_frames_after_initial_anchor"], 60)
        self.assertTrue(result["all_adjacent_histories_overlap"])
        self.assertAlmostEqual(result["presented_fps"], 120, places=4)
        self.assertAlmostEqual(result["median_interval_ms"], 8.333333)

    def test_pending_zero_and_duplicate_fence_times_are_not_frames(self):
        period, times = parse_latency(f"{STEP}\n1 0 2\n1 {PENDING} 2\n1 100 2\n2 100 3\n")
        self.assertEqual(period, STEP)
        self.assertEqual(times, {100})

    def test_lost_history_withholds_full_rate(self):
        result = analyze([snapshot(range(1, 128)), snapshot(range(200, 327))], LAYER)
        self.assertFalse(result["all_adjacent_histories_overlap"])
        self.assertIsNone(result["presented_fps"])
        self.assertLess(result["observed_minimum_rate_fps"], 120)

    def test_reset_empty_and_malformed_capture_refuse(self):
        for samples in (
            [snapshot(range(200, 327)), snapshot(range(1, 128))],
            [snapshot([]), snapshot(range(1, 128))],
            [snapshot(range(1, 128))],
        ):
            with self.assertRaises(ValueError):
                analyze(samples, LAYER)
        for text in ("", "0\n", f"{STEP}\n1 2\n", f"{STEP}\n1 {PENDING + 1} 2\n"):
            with self.assertRaises(ValueError):
                parse_latency(text)


if __name__ == "__main__":
    unittest.main()
