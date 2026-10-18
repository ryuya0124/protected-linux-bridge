"""Exercise production record framing and VTest negotiation via socketpairs."""

from pathlib import Path
import os
import socket
import struct
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]


def exact(sock, count):
    data = bytearray()
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise EOFError()
        data.extend(part)
    return bytes(data)


def header(length, command, *payload):
    return struct.pack("<" + "I" * (2 + len(payload)), length, command, *payload)


class VmProtocolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        source = Path(cls.temp.name) / "protocol.c"
        source.write_text((ROOT / "tests/fixtures/vm_protocols.c").read_text())
        cls.binary = Path(cls.temp.name) / "protocol"
        subprocess.run(
            ["cc", "-O2", "-I", str(ROOT / "native"), str(source), "-o", str(cls.binary)],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def transport(self, mode):
        client, a = socket.socketpair()
        host, b = socket.socketpair()
        for sock in (client, host):
            sock.settimeout(3)
            self.addCleanup(sock.close)
        proc = subprocess.Popen(
            [str(self.binary), mode, str(a.fileno()), str(b.fileno())],
            pass_fds=(a.fileno(), b.fileno()),
        )
        a.close()
        b.close()
        self.addCleanup(lambda: proc.kill() if proc.poll() is None else None)
        return client, host, proc

    def test_records_zero_binary_maximum_and_fragmented_header(self):
        for length in (0, 1, 1200, 65507):
            with self.subTest(length=length):
                client, host, proc = self.transport("r")
                payload = os.urandom(length)
                prefix = struct.pack("!H", length)
                client.sendall(prefix[:1])
                sender = threading.Thread(target=client.sendall, args=(prefix[1:] + payload,))
                sender.start()
                self.assertEqual(exact(host, length + 2), prefix + payload)
                sender.join(3)
                self.assertFalse(sender.is_alive())
                self.assertEqual(proc.wait(timeout=3), 0)

    def test_records_reject_oversize_truncation_and_small_buffer(self):
        for mode, data in [
            ("r", b"\xff\xe4"),
            ("r", b"\x01"),
            ("r", b"\x00\x04ab"),
            ("s", b"\x00\x09"),
        ]:
            with self.subTest(mode=mode, data=data):
                client, host, proc = self.transport(mode)
                client.sendall(data)
                client.shutdown(socket.SHUT_WR)
                self.assertEqual(proc.wait(timeout=3), 1)
                self.assertEqual(host.recv(1), b"")

    def test_writer_rejects_oversized_record(self):
        client, host, proc = self.transport("w")
        self.assertEqual(proc.wait(timeout=3), 0)
        self.assertEqual(client.recv(1), b"")

    def handshake_prefix(self, client, host):
        create = header(4, 8) + b"app\0"
        client.sendall(create + header(0, 10) + header(2, 7, 0, 0))
        self.assertEqual(exact(host, len(create) + 24), create + header(0, 10) + header(2, 7, 0, 0))
        response = header(0, 10) + header(1, 7, 0)
        host.sendall(response)
        self.assertEqual(exact(client, 20), response)

    def test_gpu_fd_free_version_zero_negotiation(self):
        for advertised in (1, 2, 3):
            with self.subTest(advertised=advertised):
                client, host, proc = self.transport("g")
                self.handshake_prefix(client, host)
                client.sendall(header(1, 11, advertised))
                self.assertEqual(exact(host, 12), header(1, 11, 0))
                host.sendall(header(1, 11, 0))
                self.assertEqual(exact(client, 12), header(1, 11, 0))
                self.assertEqual(proc.wait(timeout=3), 0)

    def test_gpu_unknown_version_rejected(self):
        client, host, proc = self.transport("g")
        self.handshake_prefix(client, host)
        client.sendall(header(1, 11, 4))
        self.assertEqual(proc.wait(timeout=3), 1)
        self.assertEqual(host.recv(1), b"")

    def test_gpu_malformed_creation_rejected(self):
        for data in (header(0, 8), header(257, 8), header(4, 9), b"\x04"):
            with self.subTest(data=data):
                client, host, proc = self.transport("g")
                client.sendall(data)
                client.shutdown(socket.SHUT_WR)
                self.assertEqual(proc.wait(timeout=3), 1)
                self.assertEqual(host.recv(1), b"")

    def test_gpu_host_cannot_accept_fd_protocol(self):
        client, host, proc = self.transport("g")
        self.handshake_prefix(client, host)
        client.sendall(header(1, 11, 3))
        self.assertEqual(exact(host, 12), header(1, 11, 0))
        host.sendall(header(1, 11, 2))
        self.assertEqual(proc.wait(timeout=3), 1)
        self.assertEqual(client.recv(1), b"")


if __name__ == "__main__":
    unittest.main()
