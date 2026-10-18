#!/usr/bin/env python3
"""Build a diskless guest memory diagnostic initramfs; does not launch any VM."""

from pathlib import Path
import argparse, platform, stat, subprocess
import hashlib
import json
from build_guest_runtime import build

p = argparse.ArgumentParser()
p.add_argument("--sdk", required=True)
p.add_argument(
    "--allocation-research",
    action="store_true",
    help="Compile small owned-allocation/cache probes; never execute them",
)
p.add_argument(
    "--policy-proposal",
    action="store_true",
    help="Compile the unapproved policy proposal helper; --plan is read-only",
)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
sdk = Path(a.sdk)
build(root, sdk)  # Ensure the pinned guest musl sysroot exists.
host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
tc = sdk / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / host
s = root / "app/build/guest/sysroot"
out = root / "artifacts"
out.mkdir(exist_ok=True)
binary = out / "plb-memory-probe"
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
        str(root / "tools/guest_memory_probe.c"),
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
(out / "memory-probe.cpio").write_bytes(data)
runner = out / "memory-runner"
subprocess.run(
    [
        str(tc / "bin/aarch64-linux-android35-clang"),
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        str(root / "tools/memory_probe_runner.c"),
        "-o",
        str(runner),
    ],
    check=True,
)
library = out / "libmemory_backing_probe.so"
subprocess.run(
    [
        str(tc / "bin/aarch64-linux-android35-clang"),
        "-O2",
        "-shared",
        "-fPIC",
        "-Wall",
        "-Wextra",
        "-Werror",
        str(root / "tools/memory_backing_probe.c"),
        "-o",
        str(library),
    ],
    check=True,
)
inputs = [
    root / "tools/build_memory_probe.py",
    root / "tools/guest_memory_probe.c",
    root / "tools/memory_probe_runner.c",
    root / "tools/memory_backing_probe.c",
    root / "native/profile_lock.h",
    binary,
    runner,
    library,
    out / "memory-probe.cpio",
]
extra_tools = []
if a.allocation_research:
    extra_tools += [
        ("backing_allocation_probe.c", "backing-allocation-probe"),
        ("owned_cache_probe.c", "owned-cache-probe"),
    ]
if a.policy_proposal:
    extra_tools.append(("memory_policy_probe.c", "memory-policy-probe"))
for source_name, output_name in extra_tools:
    source, executable = root / "tools" / source_name, out / output_name
    subprocess.run(
        [
            str(tc / "bin/aarch64-linux-android35-clang"),
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source),
            "-o",
            str(executable),
        ],
        check=True,
    )
    inputs += [source, executable]
manifest = {
    str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs
}
(out / "memory-probe-build.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("Built only; no device installation or VM launch:")
print(out / "memory-probe.cpio")
print(runner)
