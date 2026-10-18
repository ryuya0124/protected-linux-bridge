"""Production SOCKS framing/relay against real local sockets, no root or VM.
The destination-policy stub permits only our loopback echo server; the actual
Android address policy is separately covered by proxy_policy_probe.c.
"""
from pathlib import Path
import os, socket, struct, subprocess, tempfile, threading, unittest

ROOT=Path(__file__).resolve().parents[1]

def exact(s,n):
    b=bytearray()
    while len(b)<n:
        x=s.recv(n-len(b))
        if not x: raise EOFError()
        b.extend(x)
    return bytes(b)

class SocksBrokerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();d=Path(cls.tmp.name)
        (d/'main.c').write_text('''#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "stream_relay.h"
#include "datagram_protocol.h"
static int plb_network_lifetime=10;
static int forbidden_address(const struct sockaddr *a){return a->sa_family!=AF_INET||((const struct sockaddr_in*)a)->sin_addr.s_addr!=htonl(0x7f000001);}
static int tcp_connect(const char *host,const char *port){struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(atoi(port))};if(inet_pton(AF_INET,host,&a.sin_addr)!=1||forbidden_address((struct sockaddr*)&a))return -1;int fd=socket(AF_INET,SOCK_STREAM,0);if(connect(fd,(struct sockaddr*)&a,sizeof(a))){close(fd);return -1;}return fd;}
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#include "socks_broker.h"
int main(int n,char **v){if(n!=2)return 2;signal(SIGPIPE,SIG_IGN);socks_broker(atoi(v[1]));return 0;}
''')
        cls.binary=d/'broker'
        subprocess.run(['cc','-O2','-I',str(ROOT/'native'),str(d/'main.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def broker(self,greeting=True):
        a,b=socket.socketpair();a.settimeout(3)
        p=subprocess.Popen([str(self.binary),str(b.fileno())],pass_fds=(b.fileno(),));b.close()
        self.addCleanup(lambda:(p.kill(),p.wait()) if p.poll() is None else None);self.addCleanup(a.close)
        if greeting:
            for part in (b'\x05',b'\x02\x02',b'\x00'):a.sendall(part)
            self.assertEqual(exact(a,2),b'\x05\x00')
        return a,p
    def request(self,a,command,address=b'\x01\x00\x00\x00\x00\x00\x00'):
        a.sendall(bytes([5,command,0])+address)
        return exact(a,10)
    def datagram(self,a,address,data):
        a.sendall(struct.pack('!HB',len(data),3+len(address))+address+data)
    def test_authentication_and_unsupported_command(self):
        a,p=self.broker(False);a.sendall(b'\x05\x01\x02');self.assertEqual(exact(a,2),b'\x05\xff');self.assertEqual(p.wait(3),0)
        a,p=self.broker();self.assertEqual(self.request(a,2)[1],7);self.assertEqual(p.wait(3),0)
    def test_tcp_real_echo_and_half_close(self):
        server=socket.socket();server.bind(('127.0.0.1',0));server.listen();server.settimeout(3);self.addCleanup(server.close)
        data=bytes(range(256))*37;received=[]
        def echo():
            c,_=server.accept()
            with c:
                payload=bytearray()
                while True:
                    b=c.recv(997)
                    if not b:break
                    payload.extend(b)
                received.append(bytes(payload));c.sendall(payload)
        t=threading.Thread(target=echo);t.start()
        a,p=self.broker();address=b'\x01'+socket.inet_aton('127.0.0.1')+struct.pack('!H',server.getsockname()[1])
        self.assertEqual(self.request(a,1,address)[1],0);a.sendall(data);a.shutdown(socket.SHUT_WR)
        self.assertEqual(exact(a,len(data)),data);t.join(3);self.assertEqual(received,[data]);self.assertEqual(p.wait(3),0)
    def test_tcp_policy_and_zero_port(self):
        for ip,port in [('127.0.0.2',80),('127.0.0.1',0)]:
            a,p=self.broker();self.assertEqual(self.request(a,1,b'\x01'+socket.inet_aton(ip)+struct.pack('!H',port))[1],2);self.assertEqual(p.wait(3),0)
    def test_udp_binary_zero_and_large_datagrams(self):
        server=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);server.bind(('127.0.0.1',0));server.settimeout(3);self.addCleanup(server.close)
        packets=[b'',os.urandom(1280),os.urandom(8000)]
        def echo():
            for _ in packets:
                data,peer=server.recvfrom(65535);server.sendto(data,peer)
        t=threading.Thread(target=echo);t.start();a,p=self.broker();self.assertEqual(self.request(a,5)[1],0)
        address=b'\x01'+socket.inet_aton('127.0.0.1')+struct.pack('!H',server.getsockname()[1])
        for payload in packets:
            self.datagram(a,address,payload);size,header=struct.unpack('!HB',exact(a,3));self.assertEqual(header,10);self.assertEqual(exact(a,header-3),address);self.assertEqual(exact(a,size),payload)
        t.join(3);a.close();self.assertEqual(p.wait(3),0)
    def test_udp_truncated_or_invalid_records(self):
        for frame in (b'\x00',b'\xff\xff\x0a\x01'+b'\x01'*6,b'\x00\x00\x09\x01'+b'\x01'*6,b'\x00\x00\x0a\x01'+b'\x01'*4+b'\x00\x00',b'\x00\x00\x04\x09'):
            a,p=self.broker();self.assertEqual(self.request(a,5)[1],0);a.sendall(frame);a.shutdown(socket.SHUT_WR);self.assertEqual(p.wait(3),0)
    def test_udp_flow_limit(self):
        servers=[]
        for _ in range(9):
            s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('127.0.0.1',0));s.settimeout(2);servers.append(s);self.addCleanup(s.close)
        a,p=self.broker();self.request(a,5)
        for i,s in enumerate(servers):
            self.datagram(a,b'\x01'+socket.inet_aton('127.0.0.1')+struct.pack('!H',s.getsockname()[1]),b'flow')
            if i<8:self.assertEqual(s.recvfrom(100)[0],b'flow')
        self.assertEqual(p.wait(3),0)
    def test_udp_forbidden_destination(self):
        a,p=self.broker();self.request(a,5);self.datagram(a,b'\x01'+socket.inet_aton('127.0.0.2')+b'\x00\x35',b'query');self.assertEqual(p.wait(3),0)
    def test_malformed_request_fields(self):
        for request in (b'\x04\x01\x00\x01',b'\x05\x01\x01\x01',b'\x05\x01\x00\x09',b'\x05\x01\x00\x03\x03a@b\x00\x50',b'\x05\x01\x00\x03\x00'):
            a,p=self.broker();a.sendall(request);a.shutdown(socket.SHUT_WR);self.assertEqual(p.wait(3),0)

if __name__=='__main__':unittest.main()
