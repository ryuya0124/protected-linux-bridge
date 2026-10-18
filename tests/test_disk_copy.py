"""Run the production offline copy and readback verifier on private regular files."""
from pathlib import Path
import os, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class DiskCopyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(); folder=Path(cls.tmp.name)
        source=folder/'copy.c'
        source.write_text('''#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "stream_relay.h"
#include "datagram_protocol.h"
static int corrupt;
static int flush(int fd){if(corrupt&&pwrite(fd,"bad",3,3)!=3)return -1;return fsync(fd);}
#define fsync flush
#include "disk_copy.h"
int main(int argc,char **argv){if(argc!=5)return 3;corrupt=atoi(argv[4]);int from=open(argv[1],O_RDONLY),to=open(argv[2],corrupt==2?O_RDONLY:O_RDWR);if(from<0||to<0)return 4;return disk_copy_verify(from,to,strtoull(argv[3],NULL,10))?1:0;}
''')
        cls.probe=folder/'copy'
        subprocess.run(['cc','-O2','-I',str(ROOT/'native'),str(source),'-o',str(cls.probe)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def attempt(self, value, mode=0, extra=0):
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'old'; dest=Path(d)/'new'
            source.write_bytes(value)
            with dest.open('wb') as f:f.truncate(len(value)+65536)
            result=subprocess.run([str(self.probe),str(source),str(dest),str(len(value)+extra),str(mode)])
            self.assertEqual(source.read_bytes(),value, 'The original image must remain byte-for-byte intact')
            return result.returncode,dest.read_bytes()
    def test_binary_zero_holes_and_partial_final_chunk(self):
        value=bytes(65536)+os.urandom(65536)+bytes(65536)+os.urandom(17)
        code,new=self.attempt(value)
        self.assertEqual(code,0);self.assertEqual(new[:len(value)],value);self.assertEqual(new[len(value):],bytes(65536))
    def test_readback_detects_corruption(self):self.assertEqual(self.attempt(os.urandom(70000),1)[0],1)
    def test_readonly_destination_is_rejected(self):self.assertEqual(self.attempt(os.urandom(70000),2)[0],1)
    def test_truncated_source_is_rejected(self):self.assertEqual(self.attempt(os.urandom(70000),extra=1)[0],1)
