"""The explicit large-member reader still rejects whole-archive HTTP responses."""

import io
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from fetch_native_ios_rootdisk import VersionedOsArchive


class Response(io.BytesIO):
    def __init__(self, data, status=206):
        super().__init__(data)
        self.status = status
        self.headers = {"Content-Range": f"bytes 0-{len(data)-1}/{len(data)*2}", "ETag": '"fixed"'}
        self.reads = 0

    def read(self, size=-1):
        self.reads += 1
        return super().read(size)


class LargeOsFetchTest(unittest.TestCase):
    def stream(self, response):
        stream = VersionedOsArchive.__new__(VersionedOsArchive)
        stream.size = len(response.getvalue()) * 2
        stream.budget = stream.size
        stream.etag = '"fixed"'
        stream.fetched = 0
        stream.cache = {}
        stream.request = Mock(return_value=response)
        return stream

    def test_large_groups_are_reused_for_small_read_blocks(self):
        response = Response(b"a" * 1024**2 + b"b" * 7 * 1024**2)
        stream = self.stream(response)
        self.assertEqual(stream.block(0), b"a" * 1024**2)
        self.assertEqual(stream.block(1), b"b" * 1024**2)
        self.assertEqual(stream.fetched, 8 * 1024**2)
        stream.request.assert_called_once()

    def test_ignored_range_is_rejected_before_body_read(self):
        response = Response(b"x" * (8 * 1024**2), status=200)
        with self.assertRaises(ValueError):
            self.stream(response).block(0)
        self.assertEqual(response.reads, 0)

    def test_explicit_budget_exhaustion_performs_no_network_request(self):
        stream = self.stream(Response(b"x" * (8 * 1024**2)))
        stream.budget = 1
        with self.assertRaises(ValueError):
            stream.block(0)
        stream.request.assert_not_called()
