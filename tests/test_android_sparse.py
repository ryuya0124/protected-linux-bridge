"""Offline guest image parsing: sparse ranges and checksummed LP boundaries."""

import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("android_sparse", ROOT / "tools/android_sparse.py")
PARSER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PARSER)


def sparse(chunks, blocks=None):
    count = sum(length for kind, length, data in chunks)
    header = struct.pack("<I4H4I", 0xED26FF3A, 1, 0, 28, 12, 4096, blocks or count, len(chunks), 0)
    return header + b"".join(
        struct.pack("<2H2I", kind, 0, length, 12 + len(data)) + data
        for kind, length, data in chunks
    )


def with_hash(data, offset):
    result = bytearray(data)
    result[offset : offset + 32] = bytes(32)
    result[offset : offset + 32] = hashlib.sha256(result).digest()
    return result


def logical_image(name=b"vendor_a", sector=40, source=0, damage=None):
    image = bytearray(32 * 4096)
    geometry = bytearray(52)
    struct.pack_into("<2I", geometry, 0, 0x616C4467, 52)
    struct.pack_into("<3I", geometry, 40, 4096, 1, 4096)
    image[4096:4148] = with_hash(geometry, 8)
    partition = struct.pack("<36s4I", name, 1, 0, 1, 0)
    extent = struct.pack("<QIQI", 8, 0, sector, source)
    group = struct.pack("<36sIQ", b"default", 0, 0)
    device = struct.pack("<QIIQ36sI", 40, 4096, 0, len(image), b"super", 0)
    tables = partition + extent + group + device
    header = bytearray(128)
    struct.pack_into("<I2HI", header, 0, 0x414C5030, 10, 0, 128)
    struct.pack_into("<I", header, 44, len(tables))
    header[48:80] = hashlib.sha256(tables).digest()
    for offset, start, stride in [(80, 0, 52), (92, 52, 24), (104, 76, 48), (116, 124, 64)]:
        struct.pack_into("<3I", header, offset, start, 1, stride)
    image[12288:12416] = with_hash(header, 12)
    image[12416 : 12416 + len(tables)] = tables
    if damage is not None:
        image[damage] ^= 1
    return sparse([(0xCAC1, 32, bytes(image))])


class AndroidImages(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "guest.img"

    def tearDown(self):
        self.directory.cleanup()

    def open(self, data):
        self.path.write_bytes(data)
        reader = PARSER.SparseImage(self.path)
        self.addCleanup(reader.close)
        return reader

    def test_raw_fill_zero_cross_boundary(self):
        image = self.open(
            sparse([(0xCAC1, 1, b"A" * 4096), (0xCAC2, 1, b"1234"), (0xCAC3, 1, b"")])
        )
        self.assertEqual(image.read(4094, 11), b"AA123412341")
        self.assertEqual(image.read(8190, 6), b"34" + bytes(4))
        self.assertEqual(image.read(4097, 5), b"23412")

    def test_out_of_range_or_large_read_rejected(self):
        image = self.open(sparse([(0xCAC3, 1, b"")]))
        for offset, size in [(-1, 1), (4096, 1), (0, PARSER.MAX_READ + 1)]:
            with self.assertRaises(ValueError):
                image.read(offset, size)

    def test_truncated_or_wrong_sparse_size(self):
        for data in [sparse([(0xCAC1, 1, b"x")]), sparse([(0xCAC3, 1, b"")], blocks=2)]:
            self.path.write_bytes(data)
            with self.assertRaises(ValueError):
                PARSER.SparseImage(self.path)

    def test_unknown_sparse_chunk_rejected(self):
        self.path.write_bytes(sparse([(0xCAFF, 1, b"")]))
        with self.assertRaises(ValueError):
            PARSER.SparseImage(self.path)

    def test_symlink_rejected(self):
        target = self.path.with_name("real.img")
        target.write_bytes(sparse([(0xCAC3, 1, b"")]))
        self.path.symlink_to(target)
        with self.assertRaises(OSError):
            PARSER.SparseImage(self.path)

    def test_checked_logical_partition(self):
        parts = PARSER.logical_partitions(self.open(logical_image()))
        self.assertEqual(parts["vendor_a"]["extents"], [{"offset": 20480, "size": 4096}])

    def test_geometry_header_and_table_corruption(self):
        for offset in [4104, 12300, 12420]:
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                PARSER.logical_partitions(self.open(logical_image(damage=offset)))

    def test_path_traversal_partition_rejected(self):
        with self.assertRaises(ValueError):
            PARSER.logical_partitions(self.open(logical_image(name=b"../host")))

    def test_extent_before_data_or_outside_device(self):
        for sector in [0, 10000]:
            with self.subTest(sector=sector), self.assertRaises(ValueError):
                PARSER.logical_partitions(self.open(logical_image(sector=sector)))

    def test_multi_device_extent_rejected(self):
        with self.assertRaises(ValueError):
            PARSER.logical_partitions(self.open(logical_image(source=1)))


if __name__ == "__main__":
    unittest.main()
