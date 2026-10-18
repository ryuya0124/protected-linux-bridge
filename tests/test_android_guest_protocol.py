"""Validate the production packet bounds before a guest can supply image bytes."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidPacketProtocol(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / "protocol"
        subprocess.run(
            [
                "cc",
                "-O2",
                "-I",
                str(ROOT / "native"),
                str(ROOT / "tests/fixtures/android_adb_protocol.c"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_mode(self, mode):
        subprocess.run([str(self.binary), str(mode)], check=True, timeout=3)

    def test_binary_payload_and_ack(self):
        self.check_mode(0)

    def test_invalid_magic(self):
        self.check_mode(1)

    def test_oversized_packet(self):
        self.check_mode(2)

    def test_invalid_checksum(self):
        self.check_mode(3)

    def test_truncated_header(self):
        self.check_mode(4)

    def test_truncated_body(self):
        self.check_mode(5)

    def test_new_protocol_checksum_omission(self):
        self.check_mode(6)

    def test_cancellation(self):
        self.check_mode(7)
