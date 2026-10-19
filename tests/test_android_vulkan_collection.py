"""Crash evidence must survive failed commands and a missing root shell."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "vulkan_collection", ROOT / "tools/collect_android_vulkan_failure.py"
)
COLLECT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COLLECT)


class AndroidVulkanCollectionTest(unittest.TestCase):
    def invoke(self, root, run):
        arguments = ["collect", "--adb", "/test/adb", "--serial", "selected-galaxy"]
        with (
            patch.object(COLLECT, "ROOT", root),
            patch("sys.argv", arguments),
            patch.object(COLLECT.subprocess, "run", side_effect=run) as mocked,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            COLLECT.main()
        return mocked

    def test_other_device_is_refused_before_any_crash_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "identity unavailable"):
                self.invoke(root, lambda *a, **k: subprocess.CompletedProcess(a, 0, b"Pixel", b""))
            self.assertFalse((root / "artifacts").exists())

    def test_root_denial_does_not_discard_other_failed_command_output(self):
        def run(arguments, **unused):
            command = arguments[-1]
            if command == "getprop ro.product.model":
                return subprocess.CompletedProcess(arguments, 0, b"SM-S948Q\n", b"")
            if command.startswith("su -c"):
                return subprocess.CompletedProcess(arguments, 1, b"root refused", b"denied")
            return subprocess.CompletedProcess(arguments, 7, b"retained boot evidence", b"partial")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.invoke(root, run)
            output = next((root / "artifacts/android-vulkan").iterdir())
            record = json.loads((output / "collection.private.json").read_text())
            self.assertEqual(set(record), {"boot", "boot-log", "root-check"})
            self.assertEqual(record["boot"]["exit_code"], 7)
            self.assertEqual(
                (output / "boot.stdout.private.txt").read_bytes(), b"retained boot evidence"
            )
            self.assertEqual((output / "boot.stderr.private.txt").read_bytes(), b"partial")

    def test_timed_out_kernel_read_keeps_partial_bytes_and_continues(self):
        def run(arguments, **unused):
            command = arguments[-1]
            if command == "getprop ro.product.model":
                return subprocess.CompletedProcess(arguments, 0, b"SM-S948Q\n", b"")
            if "last_kmsg" in command:
                raise subprocess.TimeoutExpired(
                    arguments, 12, output=b"panic prefix", stderr=b"late"
                )
            if command == "su -c id":
                return subprocess.CompletedProcess(arguments, 0, b"uid=0(root)", b"")
            return subprocess.CompletedProcess(arguments, 0, b"readable", b"")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.invoke(root, run)
            output = next((root / "artifacts/android-vulkan").iterdir())
            record = json.loads((output / "collection.private.json").read_text())
            self.assertIsNone(record["last-kmsg"]["exit_code"])
            self.assertEqual(
                (output / "last-kmsg.stdout.private.txt").read_bytes(), b"panic prefix"
            )
            self.assertEqual((output / "last-kmsg.stderr.private.txt").read_bytes(), b"late")
            self.assertEqual(record["policies"]["exit_code"], 0)
