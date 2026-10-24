"""Independent Python/C hashing, malformed streams and private DMA tamper controls."""

import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_native_ios_block_policy import (
    write_hashes,
    validate_policy,
    TABLE_BYTES,
    TABLE_SHA256,
    RAW_BYTES,
    RAW_SHA256,
    VOLUME_UUID,
    DOMAIN,
    CHUNK_BYTES,
)


class NativeIosBlockPolicyTest(unittest.TestCase):
    def test_exact_stream_geometry_and_domain_separation(self):
        data = b"x" * (32768 + 1536)
        output = io.BytesIO()
        image, table, count = write_hashes(io.BytesIO(data), output, len(data))
        self.assertEqual(image, hashlib.sha256(data).hexdigest())
        self.assertEqual(table, hashlib.sha256(output.getvalue()).hexdigest())
        self.assertEqual(count, 3)
        rows = []
        for index, offset in enumerate(range(0, len(data), 16384)):
            block = data[offset : offset + 16384]
            rows.append(
                hashlib.sha256(
                    b"PLB-IOS-BLOCK-v1\0" + struct.pack("<QI", index, len(block)) + block
                ).digest()
            )
        self.assertEqual(output.getvalue(), b"".join(rows))
        self.assertNotEqual(rows[0], rows[1])

    def test_truncated_growing_and_invalid_images_are_refused(self):
        for data, size in ((b"x" * 511, 512), (b"x" * 513, 512), (b"", 0), (b"x", 1)):
            with self.assertRaises(ValueError):
                write_hashes(io.BytesIO(data), io.BytesIO(), size)

    def test_rehashed_substitute_table_cannot_select_a_new_trust_anchor(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "artifacts") as folder:
            directory = Path(folder)
            table = directory / "block-hashes.bin"
            with table.open("wb") as stream:
                stream.truncate(TABLE_BYTES)
            with table.open("rb") as stream:
                substituted = hashlib.file_digest(stream, "sha256").hexdigest()
            self.assertNotEqual(substituted, TABLE_SHA256)
            report = {
                "version": 1,
                "trust_model": "owner_build_time_pinned_private_guest_table",
                "image_bytes": RAW_BYTES,
                "image_sha256": RAW_SHA256,
                "volume_uuid": VOLUME_UUID,
                "chunk_bytes": CHUNK_BYTES,
                "block_count": TABLE_BYTES // 32,
                "domain_hex": DOMAIN.hex(),
                "table_bytes": TABLE_BYTES,
                "table_sha256": substituted,
                "apple_signature_authentication_qualified": False,
            }
            (directory / "policy.private.json").write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                validate_policy(directory)

    def test_actual_c_hashes_cache_private_bytes_and_refuses_corruption(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            data = bytes((i * 17 + (i >> 9) * 29) & 255 for i in range(32768 + 1536))
            hashes = directory / "hashes"
            with hashes.open("wb") as stream:
                write_hashes(io.BytesIO(data), stream, len(data))
            binary = directory / "probe"
            command = [
                "cc",
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-deprecated-declarations",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                str(ROOT / "tests/fixtures/verified_read.c"),
                "-o",
                str(binary),
            ]
            if sys.platform != "darwin":
                command.append("-lcrypto")
            subprocess.run(command, check=True)
            result = subprocess.run(
                [str(binary), str(hashes)], check=True, capture_output=True, text=True, timeout=10
            )
            self.assertIn("private snapshot controls passed", result.stdout)
