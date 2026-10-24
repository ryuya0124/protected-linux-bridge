#!/usr/bin/env python3
"""Explicitly fetch only the pinned OS ZIP member; never convert, deploy or boot it.

The metadata inspector keeps its original128MiB budget. This separate large
download reserves both future expansion space and the8GiB Mac floor. HTTPS
Range/ETag checks remain mandatory; a server's full-archive200 response is refused.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import tomllib
import zipfile

from inspect_native_ios_payload import BoundedAppleArchive, inspect


class VersionedOsArchive(BoundedAppleArchive):
    """Eight-MiB network groups expose the parent's one-MiB read blocks."""

    def __init__(self, url: str, budget: int):
        super().__init__(url)
        if not 0 < budget < self.size:
            raise ValueError("The OS-member budget must stay below the whole IPSW size")
        self.budget = budget

    def block(self, index):
        group, inside = divmod(index, 8)
        if group not in self.cache:
            start = group * 8 * 1024**2
            end = min(start + 8 * 1024**2, self.size) - 1
            if self.fetched + end - start + 1 > self.budget:
                raise ValueError("Explicit OS-member network budget exhausted")
            with self.request(
                headers={"Range": f"bytes={start}-{end}", "If-Match": self.etag}
            ) as response:
                if (
                    response.status != 206
                    or response.headers.get("Content-Range") != f"bytes {start}-{end}/{self.size}"
                    or response.headers.get("ETag") != self.etag
                ):
                    raise ValueError("The CDN did not honor the exact versioned range")
                data = response.read(end - start + 2)
            if len(data) != end - start + 1:
                raise ValueError("Truncated or oversized OS-member range")
            self.fetched += len(data)
            if len(self.cache) >= 4:
                self.cache.pop(next(iter(self.cache)))
            self.cache[group] = data
        return self.cache[group][inside * 1024**2 : (inside + 1) * 1024**2]


def fetch(source: Path, output: Path) -> dict:
    # This validates the exact device revision, unchanged board, product/build,
    # member bounds, official URL and expanded size before the large download.
    plan = inspect(source, output, os_layout=True)
    source = source.resolve(strict=True)
    output = output.resolve(strict=True)
    config = tomllib.loads((source / "board.toml").read_text())["ipsw"]
    layout = plan["main_os_layout"]
    reserve = 8 * 1024**3 + layout["expanded_bytes"] + 256 * 1024**2
    if shutil.disk_usage(output).free < reserve + layout["stored_bytes"]:
        raise ValueError("Insufficient space for OS DMG, future expansion and8GiB reserve")
    stream = VersionedOsArchive(config["url"], layout["stored_bytes"] + 64 * 1024**2)
    if stream.size != plan["archive_bytes"]:
        raise ValueError("Restore archive size changed after planning")
    h256, h384 = hashlib.sha256(), hashlib.sha384()
    actual = reported = 0
    with zipfile.ZipFile(stream) as archive:
        manifest_bytes = archive.read("BuildManifest.plist")
        if hashlib.sha256(manifest_bytes).hexdigest() != plan["manifest_sha256"]:
            raise ValueError("The actual restore manifest changed")
        manifest = plistlib.loads(manifest_bytes)
        info = archive.getinfo(layout["member"])
        if info.compress_type != zipfile.ZIP_STORED or info.file_size != layout["stored_bytes"]:
            raise ValueError("The selected stored OS member changed")
        with archive.open(info) as body, (output / "RootOS.dmg").open("xb") as target:
            while block := body.read(1024**2):
                if len(block) > info.file_size - actual:
                    raise ValueError("OS output exceeds its declared member size")
                if shutil.disk_usage(output).free < reserve + len(block):
                    raise ValueError("Reserved Mac storage floor would be crossed")
                target.write(block)
                h256.update(block)
                h384.update(block)
                actual += len(block)
                if actual == info.file_size or actual - reported >= 256 * 1024**2:
                    print(f"OS_MEMBER_DOWNLOAD bytes={actual} total={info.file_size}", flush=True)
                    reported = actual
            target.flush()
            os.fsync(target.fileno())
        # ZipExtFile also checks the whole selected member's actual CRC at EOF.
        if actual != info.file_size:
            raise ValueError("Incomplete main OS image")
        identities = [
            i
            for i in manifest["BuildIdentities"]
            if i["Info"]["DeviceClass"] == config["device_class"]
        ]
        if len(identities) != 2 or any(
            i["Manifest"]["OS"]["Info"]["Path"] != info.filename
            or i["Manifest"]["OS"]["Digest"] != h384.digest()
            for i in identities
        ):
            raise ValueError("Complete OS image disagrees with both actual restore digests")
    report = {
        "product": plan["product"],
        "version": plan["version"],
        "build": plan["build"],
        "member": layout["member"],
        "bytes": actual,
        "sha256": h256.hexdigest(),
        "sha384": h384.hexdigest(),
        "both_restore_digests_matched": True,
        "selected_member_crc_checked": True,
        "range_bytes": stream.fetched,
        "expanded_bytes_planned": layout["expanded_bytes"],
        "reserved_mac_bytes": reserve,
        "full_ipsw_downloaded": False,
        "raw_disk_created": False,
        "apple_signature_authentication_qualified": False,
        "device_modified": False,
        "vm_started": False,
        "ios_boot_verified": False,
    }
    (output / "download.private.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(fetch(args.source, args.output), indent=2))
