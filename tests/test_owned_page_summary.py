"""A contiguous run or successful short collapse must not masquerade as a 2 MiB folio."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OwnedPageSummaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / "page-summary"
        subprocess.run(
            [
                "cc",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "tools"),
                str(ROOT / "tests/fixtures/owned_page_summary.c"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_full_aligned_folios(self):
        for case in ("full-folio", "two-folios"):
            with self.subTest(case=case):
                subprocess.run([str(self.binary), case], check=True)

    def test_contiguity_is_not_a_folio(self):
        for case in (
            "base-contiguous",
            "reverse-base",
            "small-compound",
            "short-range",
            "misaligned",
        ):
            with self.subTest(case=case):
                subprocess.run([str(self.binary), case], check=True)

    def test_incomplete_observations_cannot_verify_a_folio(self):
        for case in ("broken-run", "hidden", "missing", "unreadable-flags"):
            with self.subTest(case=case):
                subprocess.run([str(self.binary), case], check=True)


if __name__ == "__main__":
    unittest.main()
