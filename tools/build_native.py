#!/usr/bin/env python3
from pathlib import Path
import argparse, subprocess, stat, platform, hashlib
from build_guest_runtime import build as build_guest
from fetch_gpu_runtime import fetch as fetch_gpu
from build_clipboard_worker import build as build_clipboard
from build_tun import build as build_tun
from fetch_storage_runtime import fetch as fetch_storage
from build_owned_display import build as build_owned_display
from build_backup_runtime import build as build_backup_runtime
import shutil

p = argparse.ArgumentParser()
p.add_argument("--sdk", required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
pre = Path(a.sdk) / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
clang = pre / host / "bin/aarch64-linux-android31-clang"
out = root / "app/build/generated/nativeAssets/runtime"
out.mkdir(parents=True, exist_ok=True)
build_backup_runtime(root, Path(a.sdk), out)
temp = root / "app/build/native"
temp.mkdir(parents=True, exist_ok=True)
identity_files = (
    [p for p in (root / "native").rglob("*") if p.is_file()]
    + list((root / "tools").glob("*.py"))
    + list((root / "tools").glob("*.c"))
    + list((root / "tools").glob("*.h"))
    + [p for p in (root / "tools/android_guest_network").rglob("*") if p.is_file()]
    + [p for p in (root / "tools/surface_probe").glob("*") if p.suffix in (".c", ".java")]
    + [root / "third_party/hev/sources.json"]
)
identity = hashlib.sha256(b"".join(p.read_bytes() for p in sorted(identity_files))).hexdigest()[:20]
base = [str(clang), "-O2", "-Wall", "-Wextra", "-Werror", f'-DPLB_BUILD_ID="{identity}"']
subprocess.run(
    base
    + ["-fPIE", "-pie", "-o", str(out / "asset-inventory"), str(root / "native/asset_inventory.c")],
    check=True,
)
subprocess.run(
    base
    + [
        "-nostdlib",
        "-static",
        "-fno-builtin",
        "-fno-stack-protector",
        "-Wl,-e,_start",
        "-o",
        str(temp / "guest-init"),
        str(root / "native/guest_init.c"),
    ],
    check=True,
)
subprocess.run(
    base
    + [
        "-fPIE",
        "-pie",
        "-o",
        str(out / "debian-bridge"),
        str(root / "native/console_bridge.c"),
        str(root / "native/network_proxy.c"),
    ],
    check=True,
)
subprocess.run(
    base
    + [
        "-DPLB_PROFILE_MAINLINE=1",
        "-fPIE",
        "-pie",
        "-o",
        str(out / "debian-mainline-bridge"),
        str(root / "native/console_bridge.c"),
        str(root / "native/network_proxy.c"),
    ],
    check=True,
)
# The Android 17 ROM lacks the separate host renderer library used by Android 16
# bundles. Do not package stale GPU/Android binaries from a previous build.
# Debian uses the separate virgl runtime below; Android guests await qualification.
for pattern in (
    "android-bridge",
    "android-large*-bridge",
    "libandroid-memory*.so",
    "gpu4g-*",
    "android-gpu4g*-bridge",
):
    for obsolete in out.glob(pattern):
        obsolete.unlink()
# Transport components only. There is deliberately no matching VMM or launch
# registration yet; packaging these does not enable Android guest startup.
display_backend, display_publisher = build_owned_display(Path(a.sdk), temp / "owned-display")
for component in (display_backend, display_publisher):
    shutil.copyfile(component, out / component.name)
# Keep a normal source build usable without private guest OS/kernel inputs.
# Never retain stale optional assets when the pinned source runtime is absent.
for obsolete in out.glob("android17-app-*"):
    obsolete.unlink()
android17_vmm = (
    root
    / "artifacts/android17-vmm/build/target/aarch64-linux-android/release/libplb_vmm_android17.so"
)
if android17_vmm.is_file():
    subprocess.run(
        [
            "python3",
            str(root / "tools/build_android17_app_runtime.py"),
            "--sdk",
            a.sdk,
            "--assets",
            str(out),
        ],
        check=True,
    )
tunnel = build_tun(root, Path(a.sdk))

fetch_gpu(root, out)
build_clipboard(root, Path(a.sdk), out)
agent = build_guest(root, Path(a.sdk))
zerofree = fetch_storage(root, out)
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


for d in ["dev", "proc", "sys", "newroot"]:
    add(d, stat.S_IFDIR | 0o755)
add("dev/console", stat.S_IFCHR | 0o600, major=5, minor=1)
add("init", stat.S_IFREG | 0o755, (temp / "guest-init").read_bytes())
add("plb-agent", stat.S_IFREG | 0o755, agent.read_bytes())
add("plb-install-gui", stat.S_IFREG | 0o755, (root / "native/plb-install-gui").read_bytes())
add("plb-install-gpu", stat.S_IFREG | 0o755, (root / "native/plb-install-gpu").read_bytes())
add(
    "plb-install-integration",
    stat.S_IFREG | 0o755,
    (root / "native/plb-install-integration").read_bytes(),
)
add("clipboard-watch.py", stat.S_IFREG | 0o755, (root / "native/clipboard_watch.py").read_bytes())
add("plb-network", stat.S_IFREG | 0o755, (root / "native/plb-network").read_bytes())
add("hev-socks5-tunnel", stat.S_IFREG | 0o755, tunnel.read_bytes())
add("plb-zerofree", stat.S_IFREG | 0o755, zerofree.read_bytes())
add("TRAILER!!!", 0)
(out / "debian-initramfs.cpio").write_bytes(data)
