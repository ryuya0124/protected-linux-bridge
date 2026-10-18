"""Simulate interruptions at each rename boundary of disk promotion."""

from pathlib import Path
import subprocess, tempfile, unittest, shutil, json, os

ROOT = Path(__file__).resolve().parents[1]


class DiskRecoveryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        folder = Path(cls.tmp.name)
        cls.base = folder / "state"
        cls.base.mkdir()
        source = folder / "state.c"
        source.write_text(
            (ROOT / "tests/fixtures/disk_recovery.c")
            .read_text()
            .replace('"PLB_TEST_BASE_PLACEHOLDER"', json.dumps(str(cls.base)))
        )
        cls.probe = folder / "recovery"
        subprocess.run(
            ["cc", "-O2", "-I", str(ROOT / "native"), str(source), "-o", str(cls.probe)], check=True
        )

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        for f in self.base.iterdir():
            f.unlink()

    def files(self, **entries):
        names = {
            "current": "current",
            "old": "debian-before-growth.qcow2",
            "new": "debian-growth.pending.qcow2",
            "marker": "disk-growth",
        }
        for name, data in entries.items():
            (self.base / names[name]).write_bytes(data)

    def recover(self):
        return subprocess.run([str(self.probe)]).returncode

    def assertOriginal(self):
        self.assertEqual((self.base / "current").read_bytes(), b"original")
        self.assertEqual(sorted(f.name for f in self.base.iterdir()), ["current"])

    def test_interrupted_copy(self):
        self.files(current=b"original", new=b"incomplete")
        self.assertEqual(self.recover(), 0)
        self.assertOriginal()

    def test_interrupted_before_first_rename(self):
        self.files(current=b"original", new=b"copied", marker=b"growth")
        self.assertEqual(self.recover(), 0)
        self.assertOriginal()

    def test_interrupted_between_renames(self):
        self.files(old=b"original", new=b"copied", marker=b"growth")
        self.assertEqual(self.recover(), 0)
        self.assertOriginal()

    def test_interrupted_after_promotion_before_confirmation(self):
        self.files(current=b"copied", old=b"original", marker=b"growth")
        self.assertEqual(self.recover(), 0)
        self.assertOriginal()

    def test_interrupted_after_commit(self):
        self.files(current=b"confirmed", marker=b"growth")
        self.assertEqual(self.recover(), 0)
        self.assertEqual((self.base / "current").read_bytes(), b"confirmed")

    def test_unexpected_old_disk_is_preserved(self):
        self.files(current=b"current", old=b"original")
        self.assertEqual(self.recover(), 1)
        self.assertEqual((self.base / "current").read_bytes(), b"current")
        self.assertEqual((self.base / "debian-before-growth.qcow2").read_bytes(), b"original")

    def test_missing_original_preserves_the_only_candidate(self):
        self.files(new=b"only-copy", marker=b"growth")
        self.assertEqual(self.recover(), 1)
        self.assertEqual((self.base / "debian-growth.pending.qcow2").read_bytes(), b"only-copy")

    def test_symlink_and_hardlink_candidates_are_rejected(self):
        self.files(current=b"original")
        outside = self.base.parent / "outside"
        outside.write_bytes(b"outside")
        pending = self.base / "debian-growth.pending.qcow2"
        pending.symlink_to(outside)
        self.assertEqual(self.recover(), 1)
        self.assertEqual(outside.read_bytes(), b"outside")
        pending.unlink()
        os.link(outside, pending)
        self.assertEqual(self.recover(), 1)
        self.assertEqual(outside.read_bytes(), b"outside")
