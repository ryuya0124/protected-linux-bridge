"""Free contiguous blocks must not be confused with MemAvailable or admission."""

import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location(
    "fragmentation_report", Path(__file__).resolve().parents[1] / "tools/fragmentation_report.py"
)
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)
MEMORY = """MemFree: 196 kB
MemAvailable: 8192000 kB
Cached: 8000000 kB
SReclaimable: 1000 kB
Unevictable: 16 kB
Mlocked: 8 kB
"""


class FragmentationReportTest(unittest.TestCase):
    def test_large_cma_blocks_do_not_become_ordinary_candidates(self):
        types = (
            "Free pages count per migrate type at order 0 1 2 3 4\n"
            "Node 0, zone Normal, type Movable 1000 500 0 0 0\n"
            "Node 0, zone Normal, type CMA 0 0 0 0 100\n"
            "Number of blocks type Movable CMA\nNode 0, zone Normal 20 10\n"
        )
        result = REPORT.summarize(
            "Node 0, zone Normal 1000 500 0 0 100", MEMORY, pagetypeinfo=types
        )
        ordinary, cma = result["migrate_types"]
        self.assertTrue(ordinary["ordinary_candidate_type"])
        self.assertEqual(ordinary["free_kib_at_least_size"]["64"], 0)
        self.assertFalse(cma["ordinary_candidate_type"])
        self.assertEqual(cma["free_kib_at_least_size"]["64"], 6400)
        with self.assertRaises(ValueError):
            REPORT.summarize(
                "Node 0, zone Normal 1",
                MEMORY,
                pagetypeinfo="Node 0, zone Normal, type CMA >100000",
            )

    def test_free_blocks_are_split_without_double_counting_within_one_size(self):
        result = REPORT.summarize("Node 0, zone Normal 1 2 3 4\n", MEMORY)
        zone = result["zones"][0]
        self.assertEqual(zone["free_kib"], 196)
        sizes = zone["eligible_by_size_kib"]
        self.assertEqual(sizes["16"]["maximum_blocks_if_split"], 11)
        self.assertEqual(sizes["32"]["maximum_blocks_if_split"], 4)
        self.assertEqual(sizes["64"]["maximum_blocks_if_split"], 0)
        self.assertEqual(result["memory"]["MemAvailable_kib"], 8192000)

    def test_rejects_missing_or_malformed_metadata(self):
        for buddy in ("", "Node 0, zone Normal -1", "not buddyinfo", "Node 0, zone Normal 1\n" * 2):
            with self.subTest(buddy=buddy), self.assertRaises(ValueError):
                REPORT.summarize(buddy, MEMORY)
        with self.assertRaises(ValueError):
            REPORT.summarize("Node 0, zone Normal 1", "MemFree: 42 kB")
        with self.assertRaises(ValueError):
            REPORT.summarize("Node 0, zone Normal 1", MEMORY, 16)


if __name__ == "__main__":
    unittest.main()
