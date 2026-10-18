#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess,stat,platform,hashlib
from build_guest_runtime import build as build_guest
from fetch_gpu_runtime import fetch as fetch_gpu
p=argparse.ArgumentParser();p.add_argument('--sdk',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
pre=Path(a.sdk)/'ndk/28.2.13676358/toolchains/llvm/prebuilt'
host='darwin-x86_64' if platform.system()=='Darwin' else 'linux-x86_64'
clang=pre/host/'bin/aarch64-linux-android31-clang'
out=root/'app/build/generated/nativeAssets/runtime';out.mkdir(parents=True,exist_ok=True)
temp=root/'app/build/native';temp.mkdir(parents=True,exist_ok=True)
identity_files=[p for p in (root/'native').iterdir() if p.is_file()]+list((root/'tools').glob('*.py'))
identity=hashlib.sha256(b''.join(p.read_bytes() for p in sorted(identity_files))).hexdigest()[:20]
base=[str(clang),'-O2','-Wall','-Wextra','-Werror',f'-DPLB_BUILD_ID="{identity}"']
subprocess.run(base+['-nostdlib','-static','-fno-builtin','-fno-stack-protector','-Wl,-e,_start','-o',str(temp/'guest-init'),str(root/'native/guest_init.c')],check=True)
subprocess.run(base+['-fPIE','-pie','-o',str(out/'debian-bridge'),str(root/'native/console_bridge.c'),str(root/'native/network_proxy.c')],check=True)
fetch_gpu(root,out)
agent=build_guest(root,Path(a.sdk))
data=bytearray();inode=1
def add(name,mode,payload=b'',major=0,minor=0):
 global inode
 n=name.encode()+b'\0';fields=[inode,mode,0,0,1,0,len(payload),0,0,major,minor,len(n),0]
 data.extend(b'070701'+''.join(f'{v:08x}' for v in fields).encode()+n);data.extend(b'\0'*(-len(data)%4));data.extend(payload);data.extend(b'\0'*(-len(data)%4));inode+=1
for d in ['dev','proc','sys','newroot']:add(d,stat.S_IFDIR|0o755)
add('dev/console',stat.S_IFCHR|0o600,major=5,minor=1);add('init',stat.S_IFREG|0o755,(temp/'guest-init').read_bytes());add('plb-agent',stat.S_IFREG|0o755,agent.read_bytes());add('plb-install-gui',stat.S_IFREG|0o755,(root/'native/plb-install-gui').read_bytes());add('plb-install-gpu',stat.S_IFREG|0o755,(root/'native/plb-install-gpu').read_bytes());add('TRAILER!!!',0)
(out/'debian-initramfs.cpio').write_bytes(data)
