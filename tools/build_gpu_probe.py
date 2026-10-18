#!/usr/bin/env python3
"""Build a read-only guest diagnostic initramfs; does not launch any VM."""

from pathlib import Path
import argparse, platform, stat, subprocess
from build_guest_runtime import build

p = argparse.ArgumentParser()
p.add_argument("--sdk", required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
sdk = Path(a.sdk)
build(root, sdk)  # Ensure the pinned guest musl sysroot exists.
host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
tc = sdk / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / host
s = root / "app/build/guest/sysroot"
out = root / "artifacts"
out.mkdir(exist_ok=True)
binary = out / "plb-gpu-readonly-probe"
runtime = next((tc / "lib/clang").glob("*/lib/linux/libclang_rt.builtins-aarch64-android.a"))
subprocess.run(
    [
        str(tc / "bin/clang"),
        "--target=aarch64-linux-musl",
        "-fuse-ld=lld",
        "-static",
        "-nostdlib",
        "-isystem",
        str(s / "include"),
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        str(s / "lib/crt1.o"),
        str(s / "lib/crti.o"),
        str(root / "tools/guest_gpu_probe.c"),
        "-L" + str(s / "lib"),
        "-lc",
        str(runtime),
        str(s / "lib/crtn.o"),
        "-o",
        str(binary),
    ],
    check=True,
)
data = bytearray()
inode = 1


def add(name, mode, payload=b"", major=0, minor=0):
    global inode
    n = name.encode() + b"\0"
    fields = [inode, mode, 0, 0, 1, 0, len(payload), 0, 0, major, minor, len(n), 0]
    data.extend(b"070701" + "".join(f"{v:08x}" for v in fields).encode() + n)
    data.extend(b"\0" * (-len(data) % 4))
    data.extend(payload)
    data.extend(b"\0" * (-len(data) % 4))
    inode += 1


for d in ("dev", "proc", "sys", "newroot"):
    add(d, stat.S_IFDIR | 0o755)
add("dev/console", stat.S_IFCHR | 0o600, major=5, minor=1)
add("init", stat.S_IFREG | 0o755, binary.read_bytes())
add("TRAILER!!!", 0)
(out / "gpu-readonly.cpio").write_bytes(data)
print(out / "gpu-readonly.cpio")
