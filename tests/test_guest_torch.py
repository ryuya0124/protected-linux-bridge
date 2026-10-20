"""Linux-only real UNIX broker framing/deadlines. Uses synthetic LED responses."""

from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "linux" and os.geteuid() == 0, "Linux root fixture only")
class GuestTorchTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = str(Path(self.directory.name) / "torch.sock")
        self.binary = Path(self.directory.name) / "plb-torch"
        subprocess.run(
            [
                "cc",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-function",
                "-I",
                str(ROOT / "native"),
                f'-DPLB_TORCH_SOCKET="{self.path}"',
                str(ROOT / "tests/fixtures/guest_torch.c"),
                "-o",
                str(self.binary),
            ],
            check=True,
        )
        self.broker = subprocess.Popen([str(self.binary), "--broker"])
        self.addCleanup(self.stop)
        for _ in range(100):
            if Path(self.path).exists():
                break
            time.sleep(0.01)
        self.assertTrue(Path(self.path).is_socket())

    def stop(self):
        self.broker.terminate()
        self.broker.wait(timeout=3)

    def command(self, *args):
        return subprocess.run([str(self.binary), *args], capture_output=True, text=True, timeout=5)

    def bridge(self, expected=0):
        peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        peer.settimeout(3)
        peer.connect(self.path)
        peer.sendall(b"B")
        self.assertEqual(peer.recv(1), bytes([expected]))
        self.addCleanup(peer.close)
        return peer

    def test_absent_bridge_and_invalid_cli_never_succeed(self):
        self.assertEqual(self.command("status").returncode, 3)
        for args in [
            ("on", "256"),
            ("on", "-1"),
            ("on", "1", "0"),
            ("on", "1", "31"),
            ("on", "1x"),
            ("off", "extra"),
        ]:
            with self.subTest(args=args):
                self.assertEqual(self.command(*args).returncode, 2)

    def test_single_bridge_and_fragmented_reply(self):
        peer = self.bridge()
        self.bridge(3)
        received = []

        def respond():
            received.append(peer.recv(4))
            peer.sendall(bytes([1, 0, 1]))
            time.sleep(0.02)
            peer.sendall(bytes([1, 5, 2, 10, 1]))

        worker = threading.Thread(target=respond)
        worker.start()
        result = self.command("on", "2", "10")
        worker.join(timeout=3)
        self.assertFalse(worker.is_alive())
        self.assertEqual(received, [bytes([1, 1, 2, 10])])
        self.assertEqual(result.returncode, 0)
        self.assertIn("owned=1", result.stdout)

    def test_disconnected_bridge_and_wrong_response_refuse(self):
        peer = self.bridge()

        def fail():
            peer.recv(4)
            peer.sendall(bytes([2, 0, 0, 0, 0, 0, 0, 0]))

        worker = threading.Thread(target=fail)
        worker.start()
        self.assertEqual(self.command("status").returncode, 3)
        worker.join(timeout=3)
        self.assertEqual(self.command("off").returncode, 3)


if __name__ == "__main__":
    unittest.main()
