#!/usr/bin/env python3
"""Build one bounded diskless CPU VM probe. Does not connect to ADB or start a VM."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import secrets
import shutil
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def build(sdk, output, debian_readonly=False, workload_mib=16, guest_kernel=None, null_audio=False):
    configurations = {
        16: 512,
        256: 512,
        384: 512,
        768: 1024,
        1280: 1536,
        1792: 2048,
        3840: 4096,
    }
    if workload_mib not in configurations:
        raise ValueError("Unsupported bounded CPU workload")
    ram_mib = configurations[workload_mib]
    output = output.resolve()
    if output.exists() or not output.is_relative_to((ROOT / "artifacts").resolve()):
        raise ValueError("Use a new private artifacts directory")
    output.mkdir(parents=True)
    nonce = secrets.token_hex(16)
    base = "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-cpu-probe-" + nonce[:12]
    header = output / "cpu_probe.generated.h"
    header.write_text(
        f'#define PLB_CPU_NONCE "{nonce}"\n#define PLB_CPU_BASE "{base}"\n'
        f"#define PLB_CPU_WORKLOAD_BYTES ({workload_mib}ULL * 1024 * 1024)\n"
        f"#define PLB_CPU_RAM_MIB {ram_mib}\n"
        f"#define PLB_CPU_RAM_BYTES ({ram_mib}ULL * 1024 * 1024)\n"
        f'#define PLB_CPU_RAM_ARGUMENT "size={ram_mib}"\n'
    )
    if debian_readonly:
        with header.open("a") as stream:
            stream.write("#define PLB_CPU_DEBIAN_READONLY 1\n")
    if null_audio:
        with header.open("a") as stream:
            stream.write("#define PLB_CPU_NULL_AUDIO 1\n")
    kernel_digest = None
    if guest_kernel is not None:
        guest_kernel = guest_kernel.resolve(strict=True)
        if not guest_kernel.is_relative_to((ROOT / "artifacts").resolve()):
            raise ValueError("Guest comparison kernels must be retained developer artifacts")
        shutil.copyfile(guest_kernel, output / "guest.Image")
        kernel_digest = hashlib.sha256((output / "guest.Image").read_bytes()).hexdigest()
        with header.open("a") as stream:
            stream.write(
                f'#define PLB_CPU_KERNEL "{base}/guest.Image"\n'
                f'#define PLB_CPU_KERNEL_SHA256 "{kernel_digest}"\n'
            )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    llvm = sdk / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / host
    musl = ROOT / "app/build/guest/sysroot"
    builtins = next((llvm / "lib/clang").glob("*/lib/linux/libclang_rt.builtins-aarch64-android.a"))
    guest = output / "guest-init"
    subprocess.run(
        [
            str(llvm / "bin/clang"),
            "--target=aarch64-linux-musl",
            "-fuse-ld=lld",
            "-static",
            "-nostdlib",
            "-isystem",
            str(musl / "include"),
            "-I",
            str(output),
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(musl / "lib/crt1.o"),
            str(musl / "lib/crti.o"),
            str(ROOT / "tools/android17_cpu_guest.c"),
            "-L" + str(musl / "lib"),
            "-lc",
            str(builtins),
            str(musl / "lib/crtn.o"),
            "-o",
            str(guest),
        ],
        check=True,
    )
    data = bytearray()

    def add(name, mode, content=b"", major=0, minor=0):
        encoded = name.encode() + b"\0"
        fields = [1, mode, 0, 0, 1, 0, len(content), 0, 0, major, minor, len(encoded), 0]
        data.extend(b"070701" + "".join(f"{v:08x}" for v in fields).encode() + encoded)
        data.extend(b"\0" * (-len(data) % 4))
        data.extend(content)
        data.extend(b"\0" * (-len(data) % 4))

    for folder in ["dev", "proc", "sys", "newroot"]:
        add(folder, stat.S_IFDIR | 0o755)
    add("dev/console", stat.S_IFCHR | 0o600, major=5, minor=1)
    add("init", stat.S_IFREG | 0o555, guest.read_bytes())
    add("TRAILER!!!", 0)
    (output / "initramfs.cpio").write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    with header.open("a") as stream:
        stream.write(f'#define PLB_CPU_INITRAMFS_SHA256 "{digest}"\n')
    subprocess.run(
        [
            str(llvm / "bin/aarch64-linux-android35-clang"),
            "-fPIE",
            "-pie",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(output),
            str(ROOT / "tools/android17_cpu_probe.c"),
            "-o",
            str(output / "cpu-probe"),
        ],
        check=True,
    )
    manifest = {
        "nonce": nonce,
        "device_directory": base,
        "ram_mib": ram_mib,
        "workload_mib": workload_mib,
        "cpus": 1,
        "timeout_seconds": 30,
        "gpu": False,
        "null_audio": null_audio,
        "disks": ["fixed Terminal root_part, read-only"] if debian_readonly else [],
        "host_policy_writes": False,
        "custom_guest_kernel_sha256": kernel_digest,
        "initramfs_sha256": digest,
        "runner_sha256": hashlib.sha256((output / "cpu-probe").read_bytes()).hexdigest(),
    }
    (output / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--debian-readonly", action="store_true")
    parser.add_argument(
        "--workload-mib", type=int, choices=(16, 256, 384, 768, 1280, 1792, 3840), default=16
    )
    parser.add_argument("--guest-kernel", type=Path)
    parser.add_argument("--null-audio", action="store_true")
    args = parser.parse_args()
    build(
        args.sdk,
        args.output,
        args.debian_readonly,
        args.workload_mib,
        args.guest_kernel,
        args.null_audio,
    )
