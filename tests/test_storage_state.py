"""Exercise retained-backup promotion, failed fsync and interrupted rename stages."""

from pathlib import Path
import json, os, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]


class StorageStateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        cls.base = folder / "vm"
        cls.base.mkdir()
        source = folder / "state.c"
        source.write_text(
            (ROOT / "tests/fixtures/storage_state.c")
            .read_text()
            .replace('"PLB_TEST_BASE_PLACEHOLDER"', json.dumps(str(cls.base)))
        )
        cls.probe = folder / "state"
        subprocess.run(
            ["cc", "-O2", "-I", str(ROOT / "native"), str(source), "-o", str(cls.probe)], check=True
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        for f in self.base.iterdir():
            f.unlink()
        self.run_action("s")

    def run_action(self, action):
        return subprocess.run([str(self.probe), action]).returncode

    def put(self, name, data=b"data"):
        (self.base / name).write_bytes(data)

    def test_verified_promotion_retains_original(self):
        self.put("current", b"original")
        self.put("storage-new.qcow2", b"new")
        self.put("storage-work.qcow2", b"work")
        self.assertEqual(self.run_action("p"), 0)
        self.assertEqual(self.run_action("r"), 0)
        self.assertEqual((self.base / "current").read_bytes(), b"new")
        self.assertEqual((self.base / "storage-previous.qcow2").read_bytes(), b"original")
        self.assertFalse((self.base / "storage-work.qcow2").exists())
        self.assertIn(b" 2 ", (self.base / "resources").read_bytes())

    def test_interrupt_each_promotion_boundary_restores_original(self):
        states = [
            {"current": b"original", "storage-new.qcow2": b"new"},
            {"storage-previous.qcow2": b"original", "storage-new.qcow2": b"new"},
            {"storage-previous.qcow2": b"original", "current": b"new"},
        ]
        for state in states:
            with self.subTest(state=state):
                self.setUp()
                for name, data in state.items():
                    self.put(name, data)
                self.put("storage-promotion", b"PLB_STORAGE_1\n")
                self.put("storage-work.qcow2")
                self.assertEqual(self.run_action("r"), 0)
                self.assertEqual((self.base / "current").read_bytes(), b"original")
                self.assertIn(b" 0 ", (self.base / "resources").read_bytes())
                self.assertFalse((self.base / "storage-previous.qcow2").exists())

    def test_original_missing_does_not_delete_only_candidates(self):
        self.put("storage-new.qcow2", b"only")
        self.put("storage-work.qcow2", b"work")
        self.assertEqual(self.run_action("r"), 1)
        self.assertEqual((self.base / "storage-new.qcow2").read_bytes(), b"only")

    def test_existing_backup_blocks_new_promotion(self):
        self.put("current", b"original")
        self.put("storage-new.qcow2", b"new")
        self.put("storage-previous.qcow2", b"older")
        self.assertEqual(self.run_action("p"), 1)
        self.assertEqual((self.base / "current").read_bytes(), b"original")
        self.assertEqual((self.base / "storage-previous.qcow2").read_bytes(), b"older")

    def test_symlink_and_hardlink_work_are_preserved(self):
        self.put("current", b"original")
        other = self.base.parent / "outside"
        other.write_bytes(b"outside")
        work = self.base / "storage-work.qcow2"
        work.symlink_to(other)
        self.assertEqual(self.run_action("r"), 1)
        self.assertEqual(other.read_bytes(), b"outside")
        work.unlink()
        os.link(other, work)
        self.assertEqual(self.run_action("r"), 1)
        self.assertEqual(other.read_bytes(), b"outside")

    def test_io_failure_at_each_promotion_step_keeps_original_recoverable(self):
        for operation, count in (("PLB_FAIL_SYNC", 14), ("PLB_FAIL_RENAME", 5)):
            for fail_at in range(1, count + 1):
                with self.subTest(operation=operation, fail_at=fail_at):
                    self.setUp()
                    self.put("current", b"original")
                    self.put("storage-new.qcow2", b"new")
                    env = os.environ.copy()
                    env[operation] = str(fail_at)
                    subprocess.run([str(self.probe), "p"], env=env)
                    self.assertEqual(self.run_action("r"), 0)
                    current = (self.base / "current").read_bytes()
                    self.assertIn(current, (b"original", b"new"))
                    if current == b"new":
                        self.assertEqual(
                            (self.base / "storage-previous.qcow2").read_bytes(), b"original"
                        )
