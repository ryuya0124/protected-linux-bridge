"""Preparation must refuse unsafe paths and malformed identities before build or deployment."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_native_uefi_runner import build
from prepare_native_uefi_probe import prepare


class NativeUefiBuilderBoundaryTest(unittest.TestCase):
    def test_non_artifact_source_is_rejected_without_output(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "artifacts") as directory:
            output = Path(directory) / "untouched"
            with self.assertRaises(ValueError):
                prepare(ROOT, output)
            self.assertFalse(output.exists())

    def test_wrong_source_commit_is_refused_before_patch(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "artifacts") as directory:
            source = Path(directory) / "source"
            source.mkdir()
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(source),
                    "-c",
                    "user.name=Probe Test",
                    "-c",
                    "user.email=probe@example.invalid",
                    "commit",
                    "-q",
                    "--allow-empty",
                    "-m",
                    "fixture",
                ],
                check=True,
            )
            output = Path(directory) / "untouched"
            with self.assertRaises(ValueError):
                prepare(source, output)
            self.assertFalse(output.exists())

    def test_bad_nonce_cannot_enter_generated_c_or_invoke_compiler(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "artifacts") as directory:
            output = Path(directory)
            (output / "probe.json").write_text(json.dumps({"nonce": '"; unexpected'}))
            snapshot = output / "host.json"
            snapshot.write_text("{}")
            with self.assertRaises(ValueError):
                build(output, snapshot, output / "nonexistent-sdk")
            self.assertFalse((output / "runner_contract.generated.h").exists())
            self.assertFalse((output / "runner").exists())
