#!/usr/bin/env python3
"""Inspect/extract pinned guest logical images without expanding the 8 GiB super.

No device, mount, flash, host kernel or VM launch operation is performed.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

from android_sparse import SparseImage, logical_partitions
from fetch_android_guest import MANIFEST, OUTPUT, check_existing

CHUNK = 1024 * 1024


def extract(image, name, partition):
    output_dir = OUTPUT / "logical-images"
    output_dir.mkdir(exist_ok=True)
    target = output_dir / (name + ".img")
    if target.exists():
        raise ValueError(f"Existing {target.name} preserved; refusing to overwrite")
    size = sum(extent["size"] for extent in partition["extents"])
    if not size or shutil.disk_usage(OUTPUT).free < size + 20 * 1024**3:
        raise ValueError("Empty partition or insufficient storage headroom")
    digest = hashlib.sha256()
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output_dir, prefix=name + ".", delete=False) as out:
            temporary = Path(out.name)
            for extent in partition["extents"]:
                at = 0
                while at < extent["size"]:
                    take = min(CHUNK, extent["size"] - at)
                    data = (
                        image.read(extent["offset"] + at, take)
                        if extent["offset"] is not None
                        else bytes(take)
                    )
                    digest.update(data)
                    if any(data):
                        out.write(data)
                    else:
                        out.seek(take, os.SEEK_CUR)
                    at += take
            out.truncate(size)
            out.flush()
            os.fsync(out.fileno())
        os.link(temporary, target)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)
    return {"name": name, "size": size, "sha256": digest.hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--extract", action="append", default=[], help="Exact logical partition name"
    )
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    member = next(member for member in manifest["members"] if member["name"] == "super.img")
    if not check_existing(OUTPUT / "super.img", member):
        parser.error("Fetch the pinned super image first with --include-super")
    image = SparseImage(OUTPUT / "super.img")
    try:
        partitions = logical_partitions(image)
        if any(name not in partitions for name in args.extract):
            parser.error("Unknown partition name; inspect the listing first")
        for name, partition in partitions.items():
            size = sum(extent["size"] for extent in partition["extents"])
            print(f"{name}: {size} bytes, {len(partition['extents'])} extents")
        for name in args.extract:
            record = extract(image, name, partitions[name])
            print(json.dumps(record))
            (OUTPUT / "logical-images" / (name + ".json")).write_text(
                json.dumps(record, indent=2) + "\n"
            )
    finally:
        image.close()


if __name__ == "__main__":
    main()
