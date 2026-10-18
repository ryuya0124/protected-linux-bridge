"""Real transport tests: binary integrity, half-close and deadlines under backpressure.
Run on Linux/macOS with Python 3 and cc. No device/root is needed.
"""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]

class RelayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        (directory / 'relay.c').write_text('''#include <stdlib.h>
#include "stream_relay.h"
int main(int n,char **v){if(n!=5)return 2;plb_relay(atoi(v[1]),atoi(v[2]),atoi(v[3]),atoi(v[4]));return 0;}
''')
        cls.binary = directory / 'relay'
        subprocess.run(['cc', '-O2', '-I', str(ROOT / 'native'), str(directory / 'relay.c'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def pair(self, seconds=8, terminal=0):
        a, relay_a = socket.socketpair()
        b, relay_b = socket.socketpair()
        for sock in (a,b,relay_a,relay_b):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4096)
        p = subprocess.Popen([str(self.binary),str(relay_a.fileno()),str(relay_b.fileno()),str(seconds),str(terminal)], pass_fds=(relay_a.fileno(),relay_b.fileno()))
        relay_a.close(); relay_b.close()
        self.addCleanup(a.close); self.addCleanup(b.close)
        self.addCleanup(lambda: p.kill() if p.poll() is None else None)
        return a,b,p

    def test_duplex_binary_backpressure_and_half_close(self):
        a,b,p = self.pair()
        sent_a = bytes(range(256))*5000
        sent_b = os.urandom(900000)
        received = [bytearray(),bytearray()]
        errors = []
        def send(sock,data):
            try: sock.sendall(data); sock.shutdown(socket.SHUT_WR)
            except Exception as error: errors.append(error)
        def receive(sock,target):
            try:
                while True:
                    chunk=sock.recv(997)
                    if not chunk:break
                    target.extend(chunk)
            except Exception as error: errors.append(error)
        jobs=[threading.Thread(target=send,args=(a,sent_a)),threading.Thread(target=send,args=(b,sent_b)),threading.Thread(target=receive,args=(b,received[0])),threading.Thread(target=receive,args=(a,received[1]))]
        for job in jobs:job.start()
        for job in jobs:job.join(10)
        self.assertFalse(any(job.is_alive() for job in jobs))
        self.assertEqual(errors,[])
        self.assertEqual(received,[sent_a,sent_b])
        self.assertEqual(p.wait(timeout=3),0)

    def test_terminal_eof_flushes_pending_output(self):
        a,b,p=self.pair(terminal=1)
        final=b'last shell output\r\n'+bytes(range(256))*16
        b.sendall(final);b.shutdown(socket.SHUT_WR)
        received=bytearray()
        while True:
            data=a.recv(1024)
            if not data:break
            received.extend(data)
        self.assertEqual(received,final)
        self.assertEqual(p.wait(timeout=3),0)

    def test_idle_deadline(self):
        a,b,p=self.pair(seconds=1)
        start=time.monotonic()
        self.assertEqual(p.wait(timeout=3),0)
        self.assertLess(time.monotonic()-start,2.5)

if __name__=='__main__':unittest.main()
