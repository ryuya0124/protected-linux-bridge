"""Refuse unrelated images before hashing/deployment and stream file hashes."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import native_ios_root_contract as contract


class NativeIosRootContractTest(unittest.TestCase):
    def write_report(self, folder):
        report = {
            "product": "iPad7,1",
            "version": "17.7.11",
            "build": "21H461",
            "source_sha256": contract.SOURCE_SHA256,
            "source_both_manifest_digests_verified": True,
            "raw_bytes": contract.RAW_BYTES,
            "raw_sha256": contract.RAW_SHA256,
            "raw_geometry_matches_udif": True,
        }
        (folder / "root-disk.private.json").write_text(json.dumps(report))
        return report

    def test_hash_stream_does_not_use_whole_file_read_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder) / "image"
            image.write_bytes(b"bounded-hash" * 1024)
            with patch.object(Path, "read_bytes", side_effect=AssertionError("unbounded read")):
                self.assertEqual(
                    contract.digest(image), hashlib.sha256(b"bounded-hash" * 1024).hexdigest()
                )

    def test_path_outside_private_artifacts_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                contract.validate(Path(folder))

    def test_report_cannot_name_a_different_source(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "artifacts") as folder:
            directory = Path(folder)
            report = self.write_report(directory)
            report["source_sha256"] = "0" * 64
            (directory / "root-disk.private.json").write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                contract.validate(directory)

    def test_wrong_geometry_and_image_symlink_escape_are_refused_before_hash(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "artifacts") as folder:
            directory = Path(folder)
            self.write_report(directory)
            image = directory / "RootOS.cdr"
            image.write_bytes(b"small")
            with patch.object(contract, "digest", side_effect=AssertionError("must reject first")):
                with self.assertRaises(ValueError):
                    contract.validate(directory)
                image.unlink()
                image.symlink_to(ROOT / "README.md")
                with self.assertRaises(ValueError):
                    contract.validate(directory)
