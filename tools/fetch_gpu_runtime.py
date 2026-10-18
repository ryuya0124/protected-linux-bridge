"""Pinned user-space Android GPU decoder. No kernel/rootfs downloads or updates."""
from pathlib import Path
import hashlib,io,tarfile,urllib.request,shutil
SHA='f1a7297bc0edf701b2955fcc9ba679707e092e1b78d9f1d620ba9e4567aa0c5b'
URL='https://packages.termux.dev/apt/termux-main/pool/main/v/virglrenderer-android/virglrenderer-android_1.3.0-1_aarch64.deb'
MIRROR='https://github.com/ryuya0124/protected-linux-bridge/releases/download/v0.2.0-preview.1/virglrenderer-android_1.3.0-1_aarch64.deb'
def fetch(root:Path,out:Path):
    cache=root/'app/build/gpu';cache.mkdir(parents=True,exist_ok=True)
    archive=cache/'virglrenderer-android_1.3.0-1_aarch64.deb'
    if not archive.exists():
        local=root/'artifacts/gpu-sources/virglrenderer-android.deb'
        if local.exists():shutil.copyfile(local,archive)
        else:
            failures=[]
            for url in (URL,MIRROR):
                try:
                    with urllib.request.urlopen(url,timeout=45) as response:payload=response.read(32*1024*1024+1)
                    if len(payload)>32*1024*1024 or hashlib.sha256(payload).hexdigest()!=SHA:raise RuntimeError('GPU runtime checksum/size mismatch')
                    temporary=archive.with_suffix('.pending');temporary.write_bytes(payload);temporary.replace(archive);break
                except (OSError,RuntimeError) as error:failures.append(str(error))
            else:raise RuntimeError('Cannot retrieve pinned GPU runtime: '+'; '.join(failures))
    data=archive.read_bytes()
    if hashlib.sha256(data).hexdigest()!=SHA:raise RuntimeError('GPU runtime checksum mismatch')
    if data[:8]!=b'!<arch>\n':raise RuntimeError('Invalid GPU package')
    offset=8;payload=None
    while offset+60<=len(data):
        header=data[offset:offset+60];name=header[:16].decode().strip().rstrip('/');size=int(header[48:58]);body=data[offset+60:offset+60+size];offset+=60+size+(size%2)
        if name.startswith('data.tar.'):payload=body;break
    if payload is None:raise RuntimeError('GPU package data missing')
    prefix='data/data/com.termux/files/usr/'
    wanted={prefix+'bin/virgl_test_server_android':'gpu-server',prefix+'opt/virglrenderer-android/lib/libepoxy.so':'gpu-libepoxy.so',prefix+'opt/virglrenderer-android/lib/libvirglrenderer.so':'gpu-libvirglrenderer.so'}
    found=set()
    with tarfile.open(fileobj=io.BytesIO(payload)) as tar:
        for member in tar.getmembers():
            name=member.name.removeprefix('./')
            if name not in wanted:continue
            if not member.isfile() or member.size>16*1024*1024:raise RuntimeError('Invalid GPU component')
            out.joinpath(wanted[name]).write_bytes(tar.extractfile(member).read());found.add(name)
    if found!=wanted.keys():raise RuntimeError('GPU runtime incomplete')
