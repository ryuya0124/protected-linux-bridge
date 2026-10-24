#!/usr/bin/env python3
"""Read bounded metadata/optional kernelcache ranges from the pinned Apple IPSW.

Never downloads the full restore archive, flashes a device, or starts a VM.
The reference iPadOS 17 payload is a compatibility anchor, not a latest-iOS claim.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import plistlib
import resource
import shutil
import struct
import subprocess
import tomllib
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEVICE_REVISION = "512350a37a80f6b368e889aee22fafbcf79a28a4"


def im4p_fields(data: bytes) -> list[tuple[int, bytes]]:
    def item(at: int, end: int):
        if at + 2 > end:
            raise ValueError("Truncated IM4P item")
        tag, length = data[at : at + 2]
        at += 2
        if length & 0x80:
            count = length & 0x7F
            if not 1 <= count <= 4 or at + count > end:
                raise ValueError("Invalid IM4P length")
            length = int.from_bytes(data[at : at + count], "big")
            at += count
        if length > end - at:
            raise ValueError("IM4P item exceeds its container")
        return tag, at, at + length

    tag, at, end = item(0, len(data))
    if tag != 0x30 or end != len(data):
        raise ValueError("Expected a complete IM4P sequence")
    children = []
    while at < end:
        tag, start, next_at = item(at, end)
        children.append((tag, data[start:next_at]))
        at = next_at
    return children


def im4p_kernel_payload(data: bytes) -> bytes:
    children = im4p_fields(data)
    if (
        len(children) < 4
        or children[0] != (0x16, b"IM4P")
        or children[1] != (0x16, b"krnl")
        or children[3][0] != 4
    ):
        raise ValueError("Not a kernel IM4P payload")
    return children[3][1]


def audit_root_hash(data: bytes, component: str, member: str, identities: list[dict]) -> dict:
    fields = im4p_fields(data)
    if [tag for tag, _ in fields] != [0x16, 0x16, 0x16, 4] or fields[0][1] != b"IM4P":
        raise ValueError("Expected a real root-hash IM4P, not a fabricated authentication manifest")
    digest = hashlib.sha384(data).digest()
    if len(identities) != 2 or any(
        identity["Manifest"][component]["Info"].get("Path") != member
        or identity["Manifest"][component].get("Digest") != digest
        or identity["Manifest"][component]["Info"].get("Personalize") is not True
        for identity in identities
    ):
        raise ValueError("Root-hash input disagrees with both target restore identities")
    return {
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "sha384": digest.hex(),
        "payload_type": fields[1][1].decode("ascii"),
        "payload_bytes": len(fields[3][1]),
        "both_restore_digests_matched": True,
        "personalization_required": True,
        "personalized_im4m_present": False,
        "signature_authentication_qualified": False,
    }


def decode_kernel(output: Path, expected: str) -> dict:
    payload = im4p_kernel_payload((output / "kernelcache.im4p").read_bytes())
    packed = output / "kernelcache.lzfse"
    raw = output / "kernelcache.fat"
    if packed.exists() or raw.exists() or (output / "kernelcache.decompressed").exists():
        raise ValueError("Do not overwrite earlier kernel decoding artifacts")
    packed.write_bytes(payload)

    def bound_output():
        resource.setrlimit(resource.RLIMIT_FSIZE, (512 * 1024**2, 512 * 1024**2))

    process = subprocess.run(
        ["/usr/bin/compression_tool", "-decode", "-a", "lzfse", "-i", str(packed), "-o", str(raw)],
        capture_output=True,
        text=True,
        timeout=60,
        preexec_fn=bound_output,
    )
    (output / "decode.private.log").write_text(process.stdout + process.stderr)
    if process.returncode:
        raise ValueError("Kernel decoding failed; inspect the private decode log")
    data = raw.read_bytes()
    if data[:4] == bytes.fromhex("cafebabe"):
        if len(data) < 8:
            raise ValueError("Truncated FAT kernel header")
        count = struct.unpack_from(">I", data, 4)[0]
        if not 0 < count < 16 or 8 + count * 20 > len(data):
            raise ValueError("Invalid FAT kernel header")
        matches = [
            entry
            for index in range(count)
            if (entry := struct.unpack_from(">5I", data, 8 + index * 20))[0] == 0x100000C
        ]
        if len(matches) != 1:
            raise ValueError("Expected one ARM64 kernel slice")
        _, _, start, size, _ = matches[0]
        if start > len(data) or size > len(data) - start:
            raise ValueError("Kernel slice outside decoded file")
        data = data[start : start + size]
    digest = hashlib.sha256(data).hexdigest()
    if (
        len(data) < 32
        or struct.unpack_from("<II", data) != (0xFEEDFACF, 0x100000C)
        or digest != expected
    ):
        raise ValueError("Decoded kernel does not match the exact reviewed board pin")
    (output / "kernelcache.decompressed").write_bytes(data)
    packed.unlink()
    raw.unlink()
    return {"sha256": digest, "bytes": len(data), "board_pin_matched": True}


class BoundedAppleArchive(io.RawIOBase):
    def __init__(self, url: str):
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme != "https" or parsed.hostname != "updates.cdn-apple.com":
            raise ValueError("Use the reviewed Apple CDN URL")
        self.url = url
        with self.request(method="HEAD") as response:
            self.size = int(response.headers["Content-Length"])
            self.etag = response.headers.get("ETag")
        if not 1 < self.size < 16 * 1024**3 or not self.etag:
            raise ValueError("Unexpected or unversioned restore archive")
        self.position = 0
        self.fetched = 0
        self.cache = {}

    def request(self, *, method="GET", headers=None):
        headers = {"Accept-Encoding": "identity", **(headers or {})}
        response = urllib.request.urlopen(
            urllib.request.Request(self.url, method=method, headers=headers), timeout=30
        )
        if response.geturl() != self.url:
            response.close()
            raise ValueError("Unexpected firmware redirect")
        return response

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=io.SEEK_SET):
        origins = {io.SEEK_SET: 0, io.SEEK_CUR: self.position, io.SEEK_END: self.size}
        if whence not in origins or not 0 <= origins[whence] + offset <= self.size:
            raise ValueError("Out-of-bounds archive seek")
        self.position = origins[whence] + offset
        return self.position

    def block(self, index):
        if index not in self.cache:
            start = index * 1024**2
            end = min(start + 1024**2, self.size) - 1
            if self.fetched + end - start + 1 > 128 * 1024**2:
                raise ValueError("The bounded range-fetch budget was exhausted")
            with self.request(
                headers={"Range": f"bytes={start}-{end}", "If-Match": self.etag}
            ) as r:
                if (
                    r.status != 206
                    or r.headers.get("Content-Range") != f"bytes {start}-{end}/{self.size}"
                    or r.headers.get("ETag") != self.etag
                ):
                    raise ValueError("The server did not honor the exact versioned byte range")
                data = r.read(end - start + 2)
            if len(data) != end - start + 1:
                raise ValueError("Truncated or oversized range response")
            self.fetched += len(data)
            if len(self.cache) >= 4:
                self.cache.pop(next(iter(self.cache)))
            self.cache[index] = data
        return self.cache[index]

    def readinto(self, buffer):
        wanted = min(len(buffer), self.size - self.position)
        if wanted > 16 * 1024**2:
            raise ValueError("An unbounded archive read was requested")
        done = 0
        while done < wanted:
            index, inside = divmod(self.position, 1024**2)
            data = self.block(index)[inside : inside + wanted - done]
            if not data:
                raise ValueError("No progress in firmware range read")
            buffer[done : done + len(data)] = data
            self.position += len(data)
            done += len(data)
        return done


def extract_bounded_member(
    archive: zipfile.ZipFile, member: str, destination: Path, limit: int
) -> dict:
    entry = archive.getinfo(member)
    if not 0 < entry.file_size <= limit or not 0 < entry.compress_size <= limit:
        raise ValueError("Oversized or empty selected payload")
    actual = 0
    digest = hashlib.sha256()
    with archive.open(entry) as source, destination.open("xb") as target:
        while block := source.read(min(1024**2, limit - actual + 1)):
            actual += len(block)
            if actual > limit or actual > entry.file_size:
                raise ValueError("Extracted payload exceeds its declared bound")
            digest.update(block)
            target.write(block)
    if actual != entry.file_size:
        raise ValueError("Incomplete selected payload")
    return {"sha256": digest.hexdigest(), "bytes": actual}


def inspect(
    source: Path,
    output: Path,
    *,
    kernelcache: bool = False,
    decode: bool = False,
    appos: bool = False,
    root_metadata: bool = False,
) -> dict:
    source = source.resolve(strict=True)
    output = output.resolve()
    private = (ROOT / "artifacts").resolve()
    if not source.is_relative_to(private) or not output.is_relative_to(private) or output.exists():
        raise ValueError("Use reviewed sources and a new ignored output directory")
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    board = (source / "board.toml").read_bytes()
    original = subprocess.check_output(["git", "-C", str(source), "show", "HEAD:board.toml"])
    if revision != DEVICE_REVISION or board != original:
        raise ValueError("Unexpected device board definition")
    if shutil.disk_usage(output.parent).free < 8 * 1024**3 + 256 * 1024**2:
        raise ValueError("Insufficient Mac build space")
    board_config = tomllib.loads(board.decode())
    config = board_config["ipsw"]
    output.mkdir(parents=True)
    stream = BoundedAppleArchive(config["url"])
    with zipfile.ZipFile(stream) as archive:
        if len(archive.infolist()) > 10000:
            raise ValueError("Unexpected restore directory size")
        entry = archive.getinfo("BuildManifest.plist")
        if entry.file_size > 16 * 1024**2:
            raise ValueError("Oversized build manifest")
        manifest_bytes = archive.read(entry)
        manifest = plistlib.loads(manifest_bytes)
        if (manifest["ProductVersion"], manifest["ProductBuildVersion"]) != (
            config["version"],
            config["build"],
        ) or config["product"] not in manifest["SupportedProductTypes"]:
            raise ValueError("Wrong Apple firmware build/product")
        identities = [
            i
            for i in manifest["BuildIdentities"]
            if i["Info"]["DeviceClass"] == config["device_class"]
            and i["Info"]["RestoreBehavior"] == "Erase"
            and int(i["ApChipID"], 16) == config["chip_id"]
            and int(i["ApBoardID"], 16) == config["board_id"]
        ]
        if len(identities) != 1:
            raise ValueError("Missing or ambiguous compatible board identity")
        components = {}
        for name, metadata in identities[0]["Manifest"].items():
            member = metadata["Info"].get("Path")
            if not member or member not in archive.namelist():
                continue
            info = archive.getinfo(member)
            components[name] = {
                "member": member,
                "bytes": info.file_size,
                "compressed_bytes": info.compress_size,
            }
        (output / "BuildManifest.plist").write_bytes(manifest_bytes)
        fetched_kernel = None
        fetched_appos = None
        fetched_root_metadata = {}
        if kernelcache or decode:
            info = archive.getinfo(components["KernelCache"]["member"])
            if info.file_size > 96 * 1024**2 or info.compress_size > 96 * 1024**2:
                raise ValueError("Oversized kernelcache member")
            destination = output / "kernelcache.im4p"
            with archive.open(info) as member, destination.open("xb") as target:
                while block := member.read(1024**2):
                    target.write(block)
            if destination.stat().st_size != info.file_size:
                raise ValueError("Incomplete kernelcache extraction")
            fetched_kernel = {
                "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
                "bytes": info.file_size,
            }
        if appos:
            # AppOS is a separate small image. Never substitute the multi-GiB
            # OS/SystemOS members or fetch the entire restore ZIP as a fallback.
            fetched_appos = extract_bounded_member(
                archive, components["Cryptex1,AppOS"]["member"], output / "AppOS.dmg", 32 * 1024**2
            )
        if root_metadata:
            target_identities = [
                i
                for i in manifest["BuildIdentities"]
                if i["Info"]["DeviceClass"] == config["device_class"]
            ]
            names = (
                "SystemVolume",
                "Cryptex1,AppVolume",
                "Cryptex1,SystemVolume",
                "StaticTrustCache",
                "Cryptex1,AppTrustCache",
                "Cryptex1,SystemTrustCache",
            )
            for name in names:
                member = components[name]["member"]
                destination = output / Path(member).name
                record = extract_bounded_member(archive, member, destination, 128 * 1024)
                digest = hashlib.sha384(destination.read_bytes()).digest()
                if digest != identities[0]["Manifest"][name]["Digest"]:
                    raise ValueError("Selected metadata digest does not match the actual manifest")
                if name.endswith("Volume"):
                    record.update(
                        audit_root_hash(destination.read_bytes(), name, member, target_identities)
                    )
                fetched_root_metadata[name] = record
    result = {
        "product": config["product"],
        "version": config["version"],
        "build": config["build"],
        "archive_bytes": stream.size,
        "range_bytes_fetched": stream.fetched,
        "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
        "components": components,
        "kernelcache": fetched_kernel,
        "appos": fetched_appos,
        "root_metadata": fetched_root_metadata,
        "root_authentication_qualified": False,
        "full_ipsw_downloaded": False,
        "os_rootfs_downloaded": False,
        "device_modified": False,
        "vm_started": False,
        "ios_boot_verified": False,
    }
    if decode:
        result["decoded_kernel"] = decode_kernel(
            output, board_config["kernel"]["prelink"]["kernelcache_sha256"]
        )
    (output / "inspection.private.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--kernelcache", action="store_true")
    parser.add_argument("--decode-kernelcache", action="store_true")
    parser.add_argument(
        "--appos", action="store_true", help="Extract only the bounded small AppOS image"
    )
    parser.add_argument(
        "--root-metadata",
        action="store_true",
        help="Extract only six small root-hash/trust-cache members; no system image",
    )
    args = parser.parse_args()
    report = inspect(
        args.source,
        args.output,
        kernelcache=args.kernelcache,
        decode=args.decode_kernelcache,
        appos=args.appos,
        root_metadata=args.root_metadata,
    )
    print(json.dumps({k: v for k, v in report.items() if k != "components"}, indent=2))
