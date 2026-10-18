#!/usr/bin/env python3
"""Build a separate Android 17 diagnostic disk as regular Mac data files.

No ADB, host launch package, mount, flashing or VM launch is performed. The
official guest boot/verity images remain byte-for-byte unchanged inside a new
GPT container. A writable qcow overlay is created separately on the device.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import uuid
import zlib

from android_ramdisk import write_lowram
from android_sparse import SparseImage
from fetch_android_guest import MANIFEST, OUTPUT, check_existing

MIB = 1024**2
SECTOR = 512
ANDROID_FILES = "/data/user/0/net.ryuya_dev.protectedlinux/files/android-guests/cuttlefish17"


def vbmeta_boot_info(directory, names):
    """Summarize the original guest VBMeta chain, including boot-image footers.

    This computes boot metadata, not a hardware attestation or signature check.
    The guest's normal libavb/dm-verity checks remain responsible for verification.
    """
    digest = hashlib.sha256()
    total = 0
    for name in names:
        with (directory / name).open("rb") as source:
            source.seek(0, os.SEEK_END)
            image_size = source.tell()
            if image_size < 256:
                raise ValueError("VBMeta image too small")
            source.seek(image_size - 64)
            footer = source.read(64)
            offset = 0
            if footer[:4] == b"AVBf":
                offset, footer_size = struct.unpack_from(">2Q", footer, 20)
                if offset + footer_size > image_size - 64:
                    raise ValueError("VBMeta footer outside guest image")
            source.seek(offset)
            header = source.read(256)
            if header[:4] != b"AVB0":
                raise ValueError("VBMeta header missing")
            auth, auxiliary = struct.unpack_from(">2Q", header, 12)
            size = 256 + auth + auxiliary
            if size > 64 * 1024 or offset + size > image_size:
                raise ValueError("VBMeta data outside guest image")
            if struct.unpack_from(">I", header, 120)[0] != 0:
                raise ValueError("Expected original guest verity/verification flags enabled")
            source.seek(offset)
            data = source.read(size)
            if len(data) != size:
                raise ValueError("Truncated guest VBMeta")
            digest.update(data)
            total += size
    return {"size": total, "hash_alg": "sha256", "digest": digest.hexdigest()}


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def copy_regular(output, base, path):
    with path.open("rb") as source:
        at = 0
        while data := source.read(MIB):
            if any(data):
                output.seek(base + at)
                output.write(data)
            at += len(data)


def copy_sparse(output, base, source):
    for logical, length, kind, physical, fill in source.chunks:
        if kind == 0xCAC3 or (kind == 0xCAC2 and not any(fill)):
            continue
        at = 0
        while at < length:
            take = min(MIB, length - at)
            data = source.read(logical + at, take)
            if any(data):
                output.seek(base + logical + at)
                output.write(data)
            at += take


def write_gpt(path, partitions):
    """New guest-only GPT; never accepts a device path or overwrites an output."""
    entries = bytearray(128 * 128)
    positions = []
    at = 2048
    for index, (name, payload, size) in enumerate(partitions):
        at = align(at, 2048)
        sectors = align(size, SECTOR) // SECTOR
        entry = index * 128
        entries[entry : entry + 16] = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4").bytes_le
        entries[entry + 16 : entry + 32] = uuid.uuid4().bytes_le
        struct.pack_into("<3Q", entries, entry + 32, at, at + sectors - 1, 0)
        encoded = name.encode("utf-16le")
        if len(encoded) > 72:
            raise ValueError("GPT label too long")
        entries[entry + 56 : entry + 56 + len(encoded)] = encoded
        positions.append((at * SECTOR, payload))
        at += sectors
    sectors = align(at + 33, 2048)
    identifier = uuid.uuid4().bytes_le

    def header(current, backup, table):
        result = bytearray(SECTOR)
        struct.pack_into(
            "<8s4I4Q16sQ3I",
            result,
            0,
            b"EFI PART",
            0x10000,
            92,
            0,
            0,
            current,
            backup,
            34,
            sectors - 34,
            identifier,
            table,
            128,
            128,
            zlib.crc32(entries),
        )
        struct.pack_into("<I", result, 16, zlib.crc32(result[:92]))
        return result

    with path.open("xb") as output:
        output.truncate(sectors * SECTOR)
        mbr = bytearray(SECTOR)
        mbr[450] = 0xEE
        struct.pack_into("<2I", mbr, 454, 1, min(sectors - 1, 0xFFFFFFFF))
        mbr[510:512] = b"\x55\xaa"
        output.write(mbr)
        output.write(header(1, sectors - 1, 2))
        output.write(entries)
        output.seek((sectors - 33) * SECTOR)
        output.write(entries)
        output.write(header(sectors - 1, 1, sectors - 33))
        for base, payload in positions:
            if isinstance(payload, SparseImage):
                copy_sparse(output, base, payload)
            else:
                copy_regular(output, base, payload)
        output.flush()
        os.fsync(output.fileno())


def build_ramdisk():
    parts = []
    for name in ("init_boot.img", "vendor_boot.img"):
        source = (OUTPUT / name).read_bytes()
        if name == "init_boot.img":
            if source[:8] != b"ANDROID!" or struct.unpack_from("<I", source, 40)[0] != 4:
                raise ValueError("Expected pinned init_boot v4")
            start = 4096
            length = struct.unpack_from("<I", source, 12)[0]
        else:
            if source[:8] != b"VNDRBOOT" or struct.unpack_from("<I", source, 8)[0] != 4:
                raise ValueError("Expected pinned vendor_boot v4")
            page = struct.unpack_from("<I", source, 12)[0]
            header_size = struct.unpack_from("<I", source, 2096)[0]
            if page not in (2048, 4096) or header_size != 2128:
                raise ValueError("Unexpected vendor_boot geometry")
            start = align(header_size, page)
            length = struct.unpack_from("<I", source, 24)[0]
        if length > 64 * MIB or start + length > len(source):
            raise ValueError("Ramdisk outside pinned boot image")
        compressed = source[start : start + length]
        result = subprocess.run(
            ["lz4", "-d", "-c"], input=compressed, capture_output=True, check=True
        )
        if len(result.stdout) > 64 * MIB or not result.stdout.startswith(b"070701"):
            raise ValueError("Unexpected guest cpio output")
        parts.append(result.stdout)
    destination = OUTPUT / "android-ramdisk.cpio"
    with destination.open("xb") as output:
        for data in parts:
            output.write(data)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu-img", default="qemu-img")
    args = parser.parse_args()
    os.umask(0o077)
    manifest = json.loads(MANIFEST.read_text())
    for member in manifest["members"]:
        if not check_existing(OUTPUT / member["name"], member):
            parser.error("Fetch all pinned images with --include-super first")
    boot_info = manifest["guest_vbmeta"]
    actual = vbmeta_boot_info(OUTPUT, boot_info["chain_order"])
    if any(actual[key] != boot_info[key] for key in actual):
        parser.error("Guest VBMeta summary does not match the pinned boot configuration")
    if shutil.disk_usage(OUTPUT).free < 24 * 1024**3:
        parser.error("Need 24 GiB free; keep at least 20 GiB after temporary preparation")
    destination = OUTPUT / "android-base.qcow2"
    raw = OUTPUT / "android-disk.raw"
    if destination.exists() or raw.exists():
        parser.error("An output already exists; refusing to overwrite")
    metadata = OUTPUT / "metadata-factory.raw"
    if not metadata.exists():
        subprocess.run(
            [
                "docker",
                "run",
                "--rm",
                "--network",
                "none",
                "--memory",
                "256m",
                "--cpus",
                "1",
                "-v",
                str(OUTPUT.resolve()) + ":/work",
                "plb-fs-tools:trixie",
                "truncate",
                "-s",
                "64M",
                "/work/metadata-factory.raw",
            ],
            check=True,
        )
        subprocess.run(
            [
                "docker",
                "run",
                "--rm",
                "--network",
                "none",
                "--memory",
                "256m",
                "--cpus",
                "1",
                "-v",
                str(OUTPUT.resolve()) + ":/work",
                "plb-fs-tools:trixie",
                "mkfs.ext4",
                "-F",
                "-b",
                "4096",
                "-O",
                "^orphan_file",
                "/work/metadata-factory.raw",
            ],
            check=True,
        )
    super_image = SparseImage(OUTPUT / "super.img")
    userdata = SparseImage(OUTPUT / "userdata.img")
    try:
        partitions = [("super", super_image, super_image.size)]
        for name in (
            "boot",
            "init_boot",
            "vendor_boot",
            "vbmeta",
            "vbmeta_system",
            "vbmeta_system_dlkm",
            "vbmeta_vendor_dlkm",
        ):
            image = OUTPUT / (name + ".img")
            partitions.append((name + "_a", image, image.stat().st_size))
        partitions += [
            ("userdata", userdata, userdata.size),
            ("metadata", metadata, metadata.stat().st_size),
        ]
        write_gpt(raw, partitions)
    finally:
        super_image.close()
        userdata.close()
    subprocess.run(
        [args.qemu_img, "convert", "-f", "raw", "-O", "qcow2", str(raw), str(destination)],
        check=True,
    )
    subprocess.run(
        [args.qemu_img, "compare", "-f", "raw", "-F", "qcow2", str(raw), str(destination)],
        check=True,
    )
    with destination.open("rb") as image:
        digest = hashlib.file_digest(image, "sha256").hexdigest()
    virtual = json.loads(
        subprocess.check_output([args.qemu_img, "info", "--output=json", str(destination)])
    )["virtual-size"]
    record = {
        "image": destination.name,
        "sha256": digest,
        "guest_only": True,
        "partitions": [{"name": name, "bytes": size} for name, payload, size in partitions],
    }
    (OUTPUT / "android-base.json").write_text(json.dumps(record, indent=2) + "\n")
    build_ramdisk()
    write_lowram(OUTPUT / "android-ramdisk.cpio", OUTPUT / "android-lowram.cpio")
    subprocess.run(
        [
            args.qemu_img,
            "create",
            "-f",
            "qcow2",
            "-F",
            "qcow2",
            "-u",
            "-b",
            ANDROID_FILES + "/android-base.qcow2",
            str(OUTPUT / "android-work.qcow2"),
            str(virtual),
        ],
        check=True,
    )
    # Only this script's verified temporary expansion is removed.
    raw.unlink()
    misc_payload = OUTPUT / "misc-payload.raw"
    with misc_payload.open("xb") as misc:
        misc.truncate(2 * MIB)
    write_gpt(OUTPUT / "android-misc.raw", [("misc", misc_payload, 2 * MIB)])
    misc_payload.unlink()
    frp_payload = OUTPUT / "frp-payload.raw"
    with frp_payload.open("xb") as frp:
        frp.truncate(MIB)
    write_gpt(OUTPUT / "android-frp.raw", [("frp", frp_payload, MIB)])
    frp_payload.unlink()
    print(json.dumps(record))
    print("Prepared private guest files only; no VM launched and no device modified.")


if __name__ == "__main__":
    main()
