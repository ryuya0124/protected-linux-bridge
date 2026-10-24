"""Exact read-only main OS disk identity, shared by preparation and diagnostics.

Hash large images incrementally. The guest verifies only the GPT prefix and
partial final page; neither check is evidence of a whole-disk guest read.
"""

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW_BYTES = 6077580800
RAW_SHA256 = "e5634d04a8ac35f024845f583394e0a35deb26a04cb0c00286a8674e25a9a4f2"
SOURCE_SHA256 = "3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d"
VOLUME_UUID = "04769D26-2CA0-4825-B443-E2E5F88300F2"
PREFIX_BYTES = 4096
PREFIX_FNV64 = 729933198653461872
TAIL_OFFSET = RAW_BYTES & ~4095
TAIL_BYTES = RAW_BYTES - TAIL_OFFSET
TAIL_FNV64 = 3972727415229066478


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fnv64(data: bytes) -> int:
    result = 14695981039346656037
    for byte in data:
        result = ((result ^ byte) * 1099511628211) & (2**64 - 1)
    return result


def validate(directory: Path) -> dict:
    directory = directory.resolve(strict=True)
    if not directory.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Use the separately prepared private main OS disk")
    report = json.loads((directory / "root-disk.private.json").read_text())
    expected = {
        "product": "iPad7,1",
        "version": "17.7.11",
        "build": "21H461",
        "source_sha256": SOURCE_SHA256,
        "source_both_manifest_digests_verified": True,
        "raw_bytes": RAW_BYTES,
        "raw_sha256": RAW_SHA256,
        "raw_geometry_matches_udif": True,
    }
    if any(report.get(key) != value for key, value in expected.items()):
        raise ValueError("Main OS preparation report does not match the pinned source")
    image = (directory / "RootOS.cdr").resolve(strict=True)
    if not image.is_relative_to(directory) or image.stat().st_size != RAW_BYTES:
        raise ValueError("Main OS raw image escaped its directory or changed geometry")
    if digest(image) != RAW_SHA256:
        raise ValueError("Main OS raw image changed since conversion")
    with image.open("rb") as stream:
        prefix = stream.read(PREFIX_BYTES)
        stream.seek(TAIL_OFFSET)
        tail = stream.read(4096)
    if (
        prefix[510:512] != b"\x55\xaa"
        or prefix[512:520] != b"EFI PART"
        or fnv64(prefix) != PREFIX_FNV64
        or len(tail) != TAIL_BYTES
        or fnv64(tail) != TAIL_FNV64
    ):
        raise ValueError("Main OS GPT prefix or partial tail changed")
    return {
        "disk_path": str(image),
        "disk_bytes": RAW_BYTES,
        "disk_sha256": RAW_SHA256,
        "prefix_bytes": PREFIX_BYTES,
        "prefix_fnv64": PREFIX_FNV64,
        "tail_offset": TAIL_OFFSET,
        "tail_bytes": TAIL_BYTES,
        "tail_fnv64": fnv64(tail),
        "volume_uuid": VOLUME_UUID,
        "fixture": "official main restore OS",
        "guest_full_disk_readback_required": False,
        "apple_signature_authentication_qualified": False,
    }
