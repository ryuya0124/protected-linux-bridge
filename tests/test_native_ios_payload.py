"""An ignored Range request must never turn into a full restore-image download."""

import io
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_native_ios_payload import BoundedAppleArchive, im4p_kernel_payload


class Response(io.BytesIO):
    def __init__(self, data=b"abcdefghijklmnop", *, status=206, etag='"fixed"'):
        super().__init__(data)
        self.status = status
        self.headers = {"Content-Range": "bytes 0-15/16", "ETag": etag}
        self.read_calls = 0

    def read(self, size=-1):
        self.read_calls += 1
        return super().read(size)


class ApplePayloadRangeTest(unittest.TestCase):
    def stream(self, response):
        stream = BoundedAppleArchive.__new__(BoundedAppleArchive)
        stream.size = 16
        stream.position = 0
        stream.fetched = 0
        stream.cache = {}
        stream.etag = '"fixed"'
        stream.request = Mock(return_value=response)
        return stream

    def test_ignored_range_and_changed_revision_are_rejected_before_body_read(self):
        for response in (Response(status=200), Response(etag='"changed"')):
            stream = self.stream(response)
            with self.assertRaises(ValueError):
                stream.block(0)
            self.assertEqual(response.read_calls, 0)

    def test_short_and_oversized_ranges_are_rejected(self):
        for data in (b"short", b"x" * 17):
            with self.assertRaises(ValueError):
                self.stream(Response(data)).block(0)

    def test_exact_range_and_cached_read(self):
        stream = self.stream(Response())
        result = bytearray(5)
        self.assertEqual(stream.readinto(result), 5)
        self.assertEqual(result, b"abcde")
        stream.seek(-3, io.SEEK_END)
        self.assertEqual(stream.readinto(result), 3)
        self.assertEqual(result[:3], b"nop")
        self.assertEqual(stream.fetched, 16)
        stream.request.assert_called_once()

    def test_budget_and_invalid_seek_stop_without_network_io(self):
        stream = self.stream(Response())
        stream.fetched = 128 * 1024**2
        with self.assertRaises(ValueError):
            stream.block(0)
        for offset, whence in ((-1, 0), (17, 0), (0, 10)):
            with self.assertRaises(ValueError):
                stream.seek(offset, whence)
        stream.request.assert_not_called()

    def test_non_apple_or_plaintext_url_never_reaches_the_network(self):
        with patch("urllib.request.urlopen") as request:
            for url in ("http://updates.cdn-apple.com/a", "https://example.org/a"):
                with self.assertRaises(ValueError):
                    BoundedAppleArchive(url)
            request.assert_not_called()

    def test_kernel_im4p_extracts_only_the_expected_payload(self):
        fields = b"\x16\x04IM4P\x16\x04krnl\x16\x01v\x04\x04data"
        encoded = b"\x30" + bytes([len(fields)]) + fields
        self.assertEqual(im4p_kernel_payload(encoded), b"data")
        with self.assertRaises(ValueError):
            im4p_kernel_payload(encoded.replace(b"krnl", b"rdsk"))

    def test_truncated_indefinite_and_oversized_der_lengths_are_rejected(self):
        for data in (b"", b"\x30", b"\x30\x80", b"\x30\x84\xff\xff\xff\xff", b"\x30\x03\x16\x04x"):
            with self.assertRaises(ValueError):
                im4p_kernel_payload(data)
