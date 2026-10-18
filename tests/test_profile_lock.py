"""Real ownership lock: competing managers/copies cannot share a writable disk."""
import os,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ProfileLock(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.build=tempfile.TemporaryDirectory();p=Path(cls.build.name)
  src=p/'probe.c';src.write_text('#include <sys/stat.h>\n#include <fcntl.h>\n#include <unistd.h>\n#include <stdlib.h>\n#include <stdio.h>\n#define PLB_OWNER_LOCK_FILE getenv("PLB_TEST_LOCK")\n#include "profile_lock.h"\nint main(int argc,char **argv){(void)argv;int fd=profile_lock();if(fd<0)return 1;puts("LOCKED");fflush(stdout);if(argc>1)getchar();close(fd);return 0;}\n')
  cls.exe=p/'probe';subprocess.run(['cc','-Wall','-Wextra','-Werror','-I',str(ROOT/'native'),str(src),'-o',str(cls.exe)],check=True)
 @classmethod
 def tearDownClass(cls):cls.build.cleanup()
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.path=Path(self.tmp.name)/'lock';self.env={**os.environ,'PLB_TEST_LOCK':str(self.path)}
 def tearDown(self):self.tmp.cleanup()
 def acquire(self):return subprocess.run([str(self.exe)],env=self.env,capture_output=True,timeout=3).returncode
 def test_private_file_created(self):self.assertEqual(self.acquire(),0);self.assertEqual(self.path.stat().st_mode&0o777,0o600)
 def test_concurrent_manager_rejected_then_release(self):
  p=subprocess.Popen([str(self.exe),'hold'],env=self.env,stdin=subprocess.PIPE,stdout=subprocess.PIPE)
  try:
   self.assertEqual(p.stdout.readline(),b'LOCKED\n');self.assertEqual(self.acquire(),1)
  finally:p.communicate(b'x',timeout=3)
  self.assertEqual(self.acquire(),0)
 def test_symlink_rejected(self):
  target=Path(self.tmp.name)/'target';target.write_text('unchanged');self.path.symlink_to(target);self.assertEqual(self.acquire(),1);self.assertEqual(target.read_text(),'unchanged')
 def test_hardlink_rejected(self):
  self.path.write_text('');self.path.chmod(0o600);os.link(self.path,Path(self.tmp.name)/'alias');self.assertEqual(self.acquire(),1)
 def test_shared_permissions_rejected(self):self.path.write_text('');self.path.chmod(0o644);self.assertEqual(self.acquire(),1)
if __name__=='__main__':unittest.main()
