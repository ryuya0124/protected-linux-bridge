#!/usr/bin/env python3
"""Prepare private, build-time-pinned block hashes; never start or modify a VM.

This is an owner-defined input policy, NOT an Apple signature or IM4M. A small
16 KiB granularity table fits the existing 64 MiB diagnostic FAT alongside XNU.
The guest hashes each private snapshot before delivering its bytes to APFS.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

from native_ios_root_contract import ROOT, RAW_BYTES, RAW_SHA256, VOLUME_UUID, digest, validate

DOMAIN = b"PLB-IOS-BLOCK-v1\0"
CHUNK_BYTES = 16384
TABLE_BYTES = ((RAW_BYTES - 1) // CHUNK_BYTES + 1) * 32
TABLE_SHA256 = "a62091ae6738616977611e632597bd9a2adfee0a57708a3f264dd9ac158e4395"


def write_hashes(source, destination, size: int) -> tuple[str, str, int]:
    if size <= 0 or size % 512:
        raise ValueError("Require an exact positive sector-aligned image")
    image_hash = hashlib.sha256()
    table_hash = hashlib.sha256()
    offset = 0
    index = 0
    while offset < size:
        length = min(CHUNK_BYTES, size - offset)
        data = source.read(length)
        if len(data) != length:
            raise ValueError("Short image while making block policy")
        image_hash.update(data)
        row = hashlib.sha256(DOMAIN + struct.pack("<QI", index, length) + data).digest()
        if destination.write(row) != len(row):
            raise ValueError("Short block table write")
        table_hash.update(row)
        offset += length
        index += 1
    if source.read(1):
        raise ValueError("Image grew beyond the policy geometry")
    return image_hash.hexdigest(), table_hash.hexdigest(), index


def prepare(root_disk: Path, output: Path) -> dict:
    root = validate(root_disk)
    output = output.resolve()
    if not output.is_relative_to((ROOT / "artifacts").resolve()) or output.exists():
        raise ValueError("Use a fresh private policy directory")
    if shutil.disk_usage(output.parent).free < 8 * 1024**3 + TABLE_BYTES + 64 * 1024**2:
        raise ValueError("Preserve the Mac8GiB floor")
    output.mkdir()
    table = output / "block-hashes.bin"
    with Path(root["disk_path"]).open("rb") as stream, table.open("xb") as destination:
        image_hash, table_hash, count = write_hashes(stream, destination, RAW_BYTES)
    if (
        image_hash != RAW_SHA256
        or table.stat().st_size != TABLE_BYTES
        or table_hash != TABLE_SHA256
    ):
        raise ValueError("Image changed or table geometry differs from the exact source")
    report = {
        "version": 1,
        "trust_model": "owner_build_time_pinned_private_guest_table",
        "image_bytes": RAW_BYTES,
        "image_sha256": image_hash,
        "volume_uuid": VOLUME_UUID,
        "chunk_bytes": CHUNK_BYTES,
        "block_count": count,
        "domain_hex": DOMAIN.hex(),
        "table_bytes": TABLE_BYTES,
        "table_sha256": table_hash,
        "apple_signature_authentication_qualified": False,
        "vm_started": False,
    }
    (output / "policy.private.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def validate_policy(directory: Path) -> dict:
    directory = directory.resolve(strict=True)
    if not directory.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Use the private prepared block policy")
    report = json.loads((directory / "policy.private.json").read_text())
    expected = {
        "version": 1,
        "trust_model": "owner_build_time_pinned_private_guest_table",
        "image_bytes": RAW_BYTES,
        "image_sha256": RAW_SHA256,
        "volume_uuid": VOLUME_UUID,
        "chunk_bytes": CHUNK_BYTES,
        "block_count": TABLE_BYTES // 32,
        "domain_hex": DOMAIN.hex(),
        "table_bytes": TABLE_BYTES,
        "table_sha256": TABLE_SHA256,
        "apple_signature_authentication_qualified": False,
    }
    table = (directory / "block-hashes.bin").resolve(strict=True)
    if (
        any(report.get(key) != value for key, value in expected.items())
        or not table.is_relative_to(directory)
        or table.stat().st_size != TABLE_BYTES
        or digest(table) != report.get("table_sha256")
    ):
        raise ValueError("Block policy changed or describes another source")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root-disk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(prepare(args.root_disk, args.output), indent=2))
