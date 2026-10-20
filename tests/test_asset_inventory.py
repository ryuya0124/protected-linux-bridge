"""Exercise the real read-only helper on private sparse files and hostile links."""

import fcntl
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AssetInventoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.base = Path(cls.build.name)
        cls.assets = cls.base / "files"
        cls.binary = cls.base / "inventory"
        subprocess.run(
            [
                "cc",
                "-Wall",
                "-Wextra",
                "-Werror",
                f'-DPLB_ASSET_ROOT="{cls.assets}"',
                str(ROOT / "native/asset_inventory.c"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.assets.mkdir()

    def tearDown(self):
        import shutil

        if self.assets.is_symlink():
            self.assets.unlink()
        else:
            shutil.rmtree(self.assets)

    def report(self):
        return json.loads(subprocess.check_output([str(self.binary)], timeout=5))

    def item(self, name):
        return next(item for item in self.report()["items"] if item["id"] == name)

    def test_empty_and_absent_are_not_read_errors(self):
        report = self.report()
        self.assertEqual(len(report["items"]), 11)
        self.assertEqual(report["busy"], -1)
        self.assertTrue(
            all(not item["present"] and not item["partial"] for item in report["items"])
        )
        self.assertGreater(report["total"], report["available"])

    def test_sparse_file_distinguishes_file_size_from_host_allocation(self):
        directory = self.assets / "vm"
        directory.mkdir()
        disk = directory / "debian.qcow2"
        with disk.open("wb") as output:
            output.truncate(64 * 1024 * 1024)
        before = disk.stat()
        item = self.item("vm")
        self.assertEqual(item["logical"], before.st_size)
        self.assertEqual(item["allocated"], before.st_blocks * 512)
        self.assertEqual(item["files"], 1)
        self.assertLess(item["allocated"], item["logical"])
        self.assertEqual(disk.stat().st_size, before.st_size)

    def test_nested_and_top_level_symlinks_are_never_followed(self):
        external = self.base / "external"
        external.mkdir(exist_ok=True)
        (external / "secret").write_text("untouched")
        directory = self.assets / "kernels"
        directory.mkdir()
        (directory / "escape").symlink_to(external, target_is_directory=True)
        (self.assets / "vm").symlink_to(external, target_is_directory=True)
        for name in ["kernels", "vm"]:
            item = self.item(name)
            self.assertTrue(item["partial"])
            self.assertEqual(item["logical"], 0)
        self.assertEqual((external / "secret").read_text(), "untouched")

    def test_root_symlink_and_extra_arguments_refused(self):
        self.assets.rmdir()
        self.assets.symlink_to(self.base, target_is_directory=True)
        self.assertNotEqual(subprocess.run([str(self.binary)]).returncode, 0)
        self.assertNotEqual(subprocess.run([str(self.binary), "--delete"]).returncode, 0)

    def test_busy_owner_is_observed_without_stopping_it(self):
        lock = self.assets / "vm-owner.lock"
        lock.touch(mode=0o600)
        with lock.open("r+") as held:
            fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertEqual(self.report()["busy"], 1)
        self.assertEqual(self.report()["busy"], 0)

    def test_deep_tree_is_bounded_and_marked_partial(self):
        directory = self.assets / "runtime"
        directory.mkdir()
        for _ in range(14):
            directory = directory / "child"
            directory.mkdir()
        (directory / "hidden").write_bytes(b"not counted")
        item = self.item("runtime")
        self.assertTrue(item["partial"])
        self.assertEqual(item["files"], 0)


if __name__ == "__main__":
    unittest.main()
