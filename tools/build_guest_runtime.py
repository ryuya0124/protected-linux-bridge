#!/usr/bin/env python3
"""Build guest USER-SPACE agent with pinned musl. Never builds/downloads a kernel."""
from pathlib import Path
import hashlib, json, os, platform, shutil, subprocess, tarfile, urllib.request
URL='https://musl.libc.org/releases/musl-1.2.5.tar.gz'
SHA256='a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4'

def build(root:Path, sdk:Path)->Path:
    host='darwin-x86_64' if platform.system()=='Darwin' else 'linux-x86_64'
    toolchain=sdk/'ndk/28.2.13676358/toolchains/llvm/prebuilt'/host
    cache=root/'app/build/guest';cache.mkdir(parents=True,exist_ok=True)
    archive=cache/'musl-1.2.5.tar.gz'
    if not archive.exists():
        local=root/'artifacts/musl-1.2.5.tar.gz'
        if local.exists():shutil.copyfile(local,archive)
        else:
            with urllib.request.urlopen(URL,timeout=45) as response:archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('musl source checksum mismatch')
    source=cache/'musl-1.2.5';sysroot=cache/'sysroot'
    if not (sysroot/'lib/libc.a').exists():
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                destination=(cache/member.name).resolve()
                if not destination.is_relative_to(cache.resolve()):raise RuntimeError('Unsafe source archive')
            tar.extractall(cache,filter='data')
        env=os.environ.copy();env.update(CC=f'{toolchain}/bin/clang --target=aarch64-linux-musl',AR=f'{toolchain}/bin/llvm-ar',RANLIB=f'{toolchain}/bin/llvm-ranlib',LDFLAGS='-fuse-ld=lld')
        with (cache/'build.log').open('w') as log:
            for command in [['./configure','--target=aarch64-linux-musl',f'--prefix={sysroot}','--disable-shared'],['make','-j4'],['make','install']]:
                subprocess.run(command,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    builtins=next((toolchain/'lib/clang').glob('*/lib/linux/libclang_rt.builtins-aarch64-android.a'))
    output=root/'app/build/native/plb-agent';output.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(toolchain/'bin/clang'),'--target=aarch64-linux-musl','-fuse-ld=lld','-static','-nostdlib','-isystem',str(sysroot/'include'),'-Wall','-Wextra','-Werror','-O2',str(sysroot/'lib/crt1.o'),str(sysroot/'lib/crti.o'),str(root/'native/guest_agent.c'),'-L'+str(sysroot/'lib'),'-lc',str(builtins),str(sysroot/'lib/crtn.o'),'-o',str(output)],check=True)
    return output
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--sdk',required=True);args=p.parse_args();print(build(Path(__file__).resolve().parents[1],Path(args.sdk)))
