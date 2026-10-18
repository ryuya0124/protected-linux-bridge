"""Read-only Android sparse image view and checked single-device LP metadata.

This deliberately supports a bounded subset for offline guest-image inspection.
It reads only regular files and never mounts a filesystem. Unsupported layouts fail.
Format reference: AOSP system/core libsparse and fs_mgr/liblp metadata_format.h.
"""

from bisect import bisect_right
import hashlib
import os
import re
import stat
import struct

MAX_READ = 16 * 1024**2
MAX_IMAGE = 16 * 1024**3


class SparseImage:
    def __init__(self, path):
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        self.file = os.fdopen(descriptor, "rb")
        try:
            # fstat checks the opened descriptor, including symlink targets.
            info = os.fstat(self.file.fileno())
            if not stat.S_ISREG(info.st_mode):
                raise ValueError("Only regular image files are accepted")
            header = self.file.read(28)
            if len(header) != 28:
                raise ValueError("Truncated sparse header")
            magic, major, minor, file_header, chunk_header, block, blocks, count, checksum = (
                struct.unpack("<I4H4I", header)
            )
            if (
                magic != 0xED26FF3A
                or major != 1
                or minor != 0
                or file_header != 28
                or chunk_header != 12
                or block < 512
                or block > 65536
                or block & (block - 1)
                or blocks * block > MAX_IMAGE
                or not blocks
                or checksum != 0
                or count > 1000000
            ):
                raise ValueError("Unsupported sparse geometry")
            self.size = blocks * block
            self.chunks = []
            self.starts = []
            logical = 0
            physical = file_header
            for _ in range(count):
                self.file.seek(physical)
                raw = self.file.read(12)
                if len(raw) != 12:
                    raise ValueError("Truncated sparse chunk")
                kind, reserved, chunk_blocks, total = struct.unpack("<2H2I", raw)
                length = chunk_blocks * block
                payload_size = total - chunk_header
                expected = {0xCAC1: length, 0xCAC2: 4, 0xCAC3: 0}.get(kind)
                # CRC chunks are not present in the pinned image. Refuse rather
                # than accepting a checksum without verifying its semantics.
                if (
                    reserved
                    or expected is None
                    or not length
                    or payload_size != expected
                    or physical + total > info.st_size
                    or logical + length > self.size
                ):
                    raise ValueError("Invalid or unsupported sparse chunk")
                payload = physical + chunk_header
                fill = None
                if kind == 0xCAC2:
                    self.file.seek(payload)
                    fill = self.file.read(4)
                self.starts.append(logical)
                self.chunks.append((logical, length, kind, payload, fill))
                logical += length
                physical += total
            if logical != self.size or physical != info.st_size:
                raise ValueError("Sparse image length mismatch")
        except BaseException:
            self.file.close()
            raise

    def close(self):
        self.file.close()

    def read(self, offset, count):
        if offset < 0 or count < 0 or count > MAX_READ or offset + count > self.size:
            raise ValueError("Image read outside bounds")
        result = bytearray()
        while count:
            index = bisect_right(self.starts, offset) - 1
            start, length, kind, physical, fill = self.chunks[index]
            relative = offset - start
            take = min(count, length - relative)
            if kind == 0xCAC1:
                self.file.seek(physical + relative)
                data = self.file.read(take)
                if len(data) != take:
                    raise ValueError("Image changed or truncated during read")
            elif kind == 0xCAC2:
                data = (fill * ((take + relative % 4 + 3) // 4))[relative % 4 :][:take]
            else:
                data = bytes(take)
            result.extend(data)
            offset += take
            count -= take
        return bytes(result)


def checked_sha256(data, checksum_offset):
    expected = data[checksum_offset : checksum_offset + 32]
    cleared = bytearray(data)
    cleared[checksum_offset : checksum_offset + 32] = bytes(32)
    if hashlib.sha256(cleared).digest() != expected:
        raise ValueError("LP metadata checksum mismatch")


def logical_partitions(image):
    """Return checked slot-0 partition extents; refuse multi-device/backup recovery."""
    geometry = image.read(4096, 52)
    magic, size = struct.unpack_from("<2I", geometry)
    if magic != 0x616C4467 or size != 52:
        raise ValueError("Unsupported LP geometry")
    checked_sha256(geometry, 8)
    maximum, slots, block = struct.unpack_from("<3I", geometry, 40)
    if not (512 <= maximum <= MAX_READ and maximum % 512 == 0 and 1 <= slots <= 4):
        raise ValueError("Invalid LP metadata bounds")
    if block < 512 or block % 512:
        raise ValueError("Invalid LP logical block size")
    offset = 12288
    first = image.read(offset, 128)
    magic, major, minor, header_size = struct.unpack_from("<I2HI", first)
    if magic != 0x414C5030 or major != 10 or minor > 2 or header_size not in (128, 256):
        raise ValueError("Unsupported LP header version")
    header = image.read(offset, header_size)
    checked_sha256(header, 12)
    table_size = struct.unpack_from("<I", header, 44)[0]
    if header_size + table_size > maximum:
        raise ValueError("LP tables exceed metadata slot")
    tables = image.read(offset + header_size, table_size)
    if hashlib.sha256(tables).digest() != header[48:80]:
        raise ValueError("LP table checksum mismatch")

    def table(descriptor, entry_size):
        start, count, stride = struct.unpack_from("<3I", header, descriptor)
        if stride != entry_size or start + count * stride > len(tables):
            raise ValueError("Invalid LP table descriptor")
        return [tables[start + n * stride : start + (n + 1) * stride] for n in range(count)]

    partitions = table(80, 52)
    extents = table(92, 24)
    groups = table(104, 48)
    devices = table(116, 64)
    if len(devices) != 1:
        raise ValueError("Only single-device guest super images are supported")
    first_sector = struct.unpack_from("<Q", devices[0])[0]
    device_size = struct.unpack_from("<Q", devices[0], 16)[0]
    minimum = offset + 2 * slots * maximum
    if device_size != image.size or first_sector * 512 < minimum:
        raise ValueError("LP device/metadata geometry mismatch")
    results = {}
    for partition in partitions:
        name = partition[:36].split(b"\0", 1)[0].decode("ascii")
        if not re.fullmatch(r"[A-Za-z0-9_]{1,36}", name) or name in results:
            raise ValueError("Unsafe or duplicate LP partition name")
        attributes, first_extent, count, group = struct.unpack_from("<4I", partition, 36)
        if first_extent + count > len(extents) or group >= len(groups):
            raise ValueError("LP partition extent/group outside table")
        ranges = []
        for entry in extents[first_extent : first_extent + count]:
            sectors, kind, sector, source = struct.unpack("<QIQI", entry)
            if kind == 0 and (
                source != 0 or sector < first_sector or (sector + sectors) * 512 > image.size
            ):
                raise ValueError("LP linear extent outside device")
            if kind not in (0, 1) or (kind == 1 and (source != 0 or sector != 0)):
                raise ValueError("Unsupported LP extent target")
            ranges.append({"offset": sector * 512 if kind == 0 else None, "size": sectors * 512})
        if sum(extent["size"] for extent in ranges) > image.size:
            raise ValueError("LP partition exceeds device capacity")
        results[name] = {"attributes": attributes, "extents": ranges}
    return results
