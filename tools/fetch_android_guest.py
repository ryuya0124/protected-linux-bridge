#!/usr/bin/env python3
"""Fetch pinned Android 17 guest images only; never install, flash or launch them.

HTTP ranges avoid caching the full archive and unrelated host launch packages.
Use --include-super explicitly to fetch the large guest system/vendor container.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import urllib.request
import zlib

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "third_party/android-cuttlefish17.json"
OUTPUT = ROOT / "artifacts/android-display/cuttlefish17"
CHUNK = 1024 * 1024
MIN_FREE = 20 * 1024**3


def ranged(url, start, count, archive_size):
    request = urllib.request.Request(url, headers={"Range": f"bytes={start}-{start + count - 1}"})
    response = urllib.request.urlopen(request, timeout=60)
    expected = f"bytes {start}-{start + count - 1}/{archive_size}"
    if response.status != 206 or response.headers.get("Content-Range") != expected:
        response.close()
        raise ValueError("Server did not honor the bounded range; refusing full download")
    return response


def check_existing(path, member):
    if not path.exists():
        return False
    digest = hashlib.sha256()
    crc = 0
    size = 0
    with path.open("rb") as stream:
        while data := stream.read(CHUNK):
            digest.update(data)
            crc = zlib.crc32(data, crc)
            size += len(data)
    if size != member["size"] or crc != member["crc"]:
        raise ValueError(f"Existing {path.name} does not match the pinned member; preserved")
    if member.get("sha256") and digest.hexdigest() != member["sha256"]:
        raise ValueError(f"Existing {path.name} SHA256 mismatch; preserved")
    return True


def fetch_member(manifest, member):
    name = member["name"]
    if Path(name).name != name or member["method"] != 8:
        raise ValueError("Only the pinned, flat deflated image members are accepted")
    target = OUTPUT / name
    if check_existing(target, member):
        print(f"Verified cached {name}", flush=True)
        return
    if shutil.disk_usage(OUTPUT).free < MIN_FREE + member["size"]:
        raise RuntimeError("Not enough Mac storage to retain 20 GiB free after download")
    url, archive_size = manifest["archive_url"], manifest["archive_size"]
    with ranged(url, member["offset"], 256, archive_size) as response:
        header = response.read(257)
    if len(header) != 256 or header[:4] != b"PK\x03\x04":
        raise ValueError("Unexpected ZIP local header")
    fields = struct.unpack_from("<4s5H3L2H", header)
    name_length, extra_length = fields[-2:]
    if header[30 : 30 + name_length].decode() != name:
        raise ValueError("ZIP member identity mismatch")
    start = member["offset"] + 30 + name_length + extra_length
    decoder = zlib.decompressobj(-15)
    crc = 0
    size = 0
    digest = hashlib.sha256()
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=OUTPUT, prefix=name + ".", delete=False) as output:
            temporary = Path(output.name)
            with ranged(url, start, member["compressed"], archive_size) as response:
                remaining = member["compressed"]
                while remaining:
                    compressed = response.read(min(CHUNK, remaining))
                    if not compressed:
                        raise ValueError("Truncated ZIP member")
                    remaining -= len(compressed)
                    while compressed:
                        data = decoder.decompress(compressed, CHUNK)
                        compressed = decoder.unconsumed_tail
                        size += len(data)
                        if size > member["size"]:
                            raise ValueError("Member exceeds pinned output bound")
                        crc = zlib.crc32(data, crc)
                        digest.update(data)
                        output.write(data)
            if not decoder.eof or decoder.unused_data or size != member["size"]:
                raise ValueError("Invalid compressed stream or image size")
            if crc != member["crc"]:
                raise ValueError("ZIP member CRC mismatch")
            if member.get("sha256") and digest.hexdigest() != member["sha256"]:
                raise ValueError("Pinned member SHA256 mismatch")
            output.flush()
            os.fsync(output.fileno())
        # Refuse replacing an image that appeared during download.
        os.link(temporary, target)
        print(f"Downloaded {name}: {size} bytes, SHA256 {digest.hexdigest()}", flush=True)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-super", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for member in manifest["members"]:
        if member["name"] != "super.img" or args.include_super:
            fetch_member(manifest, member)
    print("Guest data files only. No Android device, kernel or partition was changed.")


if __name__ == "__main__":
    main()
