#!/usr/bin/env python3
"""Build a separately pinned experimental Android 17 app runtime, never a VM.

This uses the reviewed baseline source-built DSOs, not the stock VMM or a guest
Vulkan/YUV experiment. OS disks remain separately prepared, app-private inputs.
"""

from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]

parser = argparse.ArgumentParser(
    description="Build the optional pinned Android 17 experimental app runtime"
)
parser.add_argument("--sdk", required=True, type=Path)
parser.add_argument("--assets", required=True, type=Path)
a = parser.parse_args()
base = root / "artifacts/android17-vmm"
out = root / "app/build/native/android17-app"
out.mkdir(parents=True, exist_ok=True)
sdk = a.sdk
java = Path(os.environ["JAVA_HOME"]) / "bin"
bt = sdk / "build-tools/36.1.0"
android = sdk / "platforms/android-37.0/android.jar"
host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
bin = sdk / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / host / "bin"
cc = bin / "aarch64-linux-android35-clang"
cxx = bin / "aarch64-linux-android35-clang++"
dest = "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-app-runtime"
profile = "/data/user/0/net.ryuya_dev.protectedlinux/files/android-guests/h17mfix"
expected_libraries = {
    "libplb_vmm_android17.so": (
        base / "build/target/aarch64-linux-android/release/libplb_vmm_android17.so",
        "7a9b1bdd7bf9c5881eb735e99101530852d1253d145756f59c4f2f558993d2fd",
    ),
    "libgfxstream_backend.so": (
        base / "host-libs/libgfxstream_backend.so",
        "1d37c15353b863bf49710d5f1b59471aaa0c443230434722d3e7d804596301cb",
    ),
}
for name, (path, expected) in expected_libraries.items():
    if (
        not path.is_file()
        or path.is_symlink()
        or hashlib.sha256(path.read_bytes()).hexdigest() != expected
    ):
        raise RuntimeError("Missing/incompatible pinned baseline: " + name)


def run(a):
    subprocess.run(list(map(str, a)), check=True)


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


config = {
    "PLB_ANDROID4G_RUNTIME": dest,
    "PLB_ANDROID4G_BASE": profile,
    "PLB_ANDROID4G_KERNEL": dest + "/guest.Image",
    "PLB_ANDROID4G_PROXY": dest + "/adopt-vmm",
    "PLB_ANDROID4G_MANAGER": dest + "/vm-controller",
    "PLB_ANDROID4G_VMM": dest + "/launch-vmm",
}
(out / "android17_config.generated.h").write_text(
    "\n".join("#define " + k + " " + json.dumps(v) for k, v in config.items())
    + "\n#define PLB_ANDROID4G_MEMORY_MIB 1024\n"
)


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError("Reviewed source contract changed: " + before)
    return text.replace(before, after)


args = (root / "tools/android4g_arguments.h").read_text()
for before, after in [
    ("plb-android17-gpu4096", "plb-host17-owned-gles"),
    ('"size=4096"', '"size=1024"'),
    ("num-cores=2", "num-cores=1"),
    ('"run",', '"run", "--android-display-service", "plb-owned-gles",'),
    ("androidboot.config.low_ram=false", "androidboot.config.low_ram=true"),
    (
        "surfaceless=true,width=480,height=800,",
        "surfaceless=true,displays=[[mode=windowed[480,800],refresh-rate=120]],",
    ),
]:
    args = replace_once(args, before, after)
(out / "android17_arguments.generated.h").write_text(args)
flags = [
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pthread",
    "-I",
    out,
    "-I",
    root / "tools",
    "-I",
    root / "native",
    "-DPLB_SURFACE_PRODUCT=1",
    "-DPLB_ANDROID4G_CUSTOM_GUEST=1",
    "-DPLB_PREALLOCATED_ANDROID_PROXY=1",
    "-DPLB_ANDROID4G_LARGER_DMA_POOL=1",
    "-DPLB_ANDROID4G_BUFFER_CAPSET=1",
    "-DPLB_ANDROID4G_DISABLE_RENDERER_VULKAN=1",
    "-DPLB_ANDROID4G_NORMAL_FIRST=1",
    "-DPLB_PREALLOCATED_REQUIRE_ALL_THP=1",
]
run([cc, *flags, root / "tools/owned_display_probe/launch_vmm.c", "-ldl", "-o", out / "launch-vmm"])
run(
    [
        cxx,
        "-DPLB_OWNED_VMM_LIBRARY=" + json.dumps(dest + "/libplb_vmm_android17.so"),
        "-shared",
        "-fPIC",
        "-static-libstdc++",
        "-std=c++17",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        root / "tools/owned_display_probe/vmm_entry.cpp",
        "-ldl",
        "-o",
        out / "libplb-vmm-entry.so",
    ]
)
(out / "RootOwnedVmm.java").write_text(
    (root / "tools/owned_display_probe/RootOwnedVmm.java")
    .read_text()
    .replace("android17-owned-runtime", "android17-app-runtime")
)
classes = out / "classes"
classes.mkdir(exist_ok=True)
dex = out / "dex"
dex.mkdir(exist_ok=True)
publisher = (
    root
    / "native/owned_display/root/net/ryuya_dev/protectedlinux/displayroot/RootOwnedDisplayPublisher.java"
)
(out / "RootOwnedDisplayPublisher.java").write_text(
    publisher.read_text().replace("android17-owned-runtime", "android17-app-runtime")
)
run(
    [
        java / "javac",
        "--release",
        "17",
        "-cp",
        str(android) + ":" + str(root / "app/build/native/owned-display/classes"),
        "-d",
        classes,
        out / "RootOwnedVmm.java",
        out / "RootOwnedDisplayPublisher.java",
    ]
)
run(
    [
        bt / "d8",
        "--release",
        "--min-api",
        "36",
        "--lib",
        android,
        "--output",
        dex,
        *classes.rglob("*.class"),
        *[
            p
            for p in (root / "app/build/native/owned-display/classes").rglob("*.class")
            if p.name != "RootOwnedDisplayPublisher.class"
        ],
    ]
)
with zipfile.ZipFile(out / "owned-vmm.jar", "w") as z:
    z.writestr(
        zipfile.ZipInfo("classes.dex", date_time=(1980, 1, 1, 0, 0, 0)),
        (dex / "classes.dex").read_bytes(),
    )
# Overlay the reviewed prefault-only allocator in this private Android 17 build.
allocator = (root / "tools/preallocated_vm_probe.c").read_text()
allocator = allocator.replace('"android4g_config.h"', '"android17_config.generated.h"').replace(
    '"android4g_arguments.h"', '"android17_arguments.generated.h"'
)
allocator = allocator.replace(
    "6.12.30-android16-5-pd30ff70-abogkiS948QOPS1AZHL-4k",
    "6.12.69-android16-6-pee899be-abogkiS948QOPU1BZI9-4k",
).replace("S948QOPS1AZHL", "S948QOPU1BZI9")
allocator = allocator.replace(
    '"/apex/com.android.virt/lib64:/system/lib64"',
    json.dumps(dest + ":/apex/com.android.virt/lib64:/system/lib64"),
)
old_check = """    if (!pinned_file(VMM, PLB_PREALLOCATED_VMM_SHA256) ||
        !pinned_file(GUEST_KERNEL, PLB_PREALLOCATED_KERNEL_SHA256) ||
        !pinned_file(INITRAMFS, PLB_PREALLOCATED_INITRAMFS_SHA256) ||
        !available(memory + (pin_check ? 768 : 0))) {"""
new_check = """    int vmm_ok = pinned_file(VMM, PLB_PREALLOCATED_VMM_SHA256);
    int kernel_ok = pinned_file(GUEST_KERNEL, PLB_PREALLOCATED_KERNEL_SHA256);
    int initrd_ok = pinned_file(INITRAMFS, PLB_PREALLOCATED_INITRAMFS_SHA256);
    int available_ok = available(memory + (pin_check ? 768 : 0));
    fprintf(stderr, "ANDROID17_RAM_ADMISSION vmm=%d kernel=%d initrd=%d available=%d memory_mib=%d\\n",
            vmm_ok, kernel_ok, initrd_ok, available_ok, memory);
    if (!vmm_ok || !kernel_ok || !initrd_ok || !available_ok) {"""
assert allocator.count(old_check) == 1
allocator = allocator.replace(old_check, new_check)
(out / "preallocated_vm17.generated.c").write_text(allocator)
# Pin the launcher, kernel and initramfs before entering ART; controller also pins DSOs.
kernel = root / "artifacts/android4g-gpu/guest-kernel-system-heap/Image-android17-dma-sync"
ramdisk = root / "artifacts/android4g-gpu/probe-build-custom/android-lowram.cpio"
assert digest(kernel) == "4ae0f63515fe8a16945007b26423c0a6eccb8b04119259bd424d5e15894b52f5"
assert digest(ramdisk) == "4ef88a3ce9fdda207e0bfabe1d28ae24319848dcedf613d60ba61e0936f620d5", str(
    ramdisk
)
shutil.copyfile(kernel, out / "guest.Image")
shutil.copyfile(ramdisk, out / "android-lowram.cpio")
(out / "preallocated_vm.generated.h").write_text(
    "".join(
        "#define PLB_PREALLOCATED_" + k + "_SHA256 " + json.dumps(v) + "\n"
        for k, v in {
            "VMM": digest(out / "launch-vmm"),
            "KERNEL": digest(kernel),
            "INITRAMFS": digest(ramdisk),
        }.items()
    )
)
run([cc, *flags, out / "preallocated_vm17.generated.c", "-o", out / "adopt-vmm"])
# Same reviewed guardian, extended only to the six already approved nodes.
guard = (root / "native/small_shmem_guard.h").read_text()
needle = '    "/sys/kernel/mm/transparent_hugepage/hugepages-64kB/shmem_enabled",'
assert guard.count(needle) == 1
guard = guard.replace(
    needle,
    needle
    + "\n"
    + "".join(
        '    "/sys/kernel/mm/transparent_hugepage/hugepages-' + str(size) + 'kB/shmem_enabled",\n'
        for size in (128, 256, 512)
    ),
)
guard = (
    guard.replace("i < 3", "i < 6")
    .replace("i = 2;", "i = 5;")
    .replace(
        "sizes_kib=16,32,64 restore_after_seconds=10",
        "sizes_kib=16,32,64,128,256,512 bounded_seconds=40",
    )
)
(out / "six_shmem_guard.generated.h").write_text(guard)
run(
    [
        cc,
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        root / "tools",
        "-DPLB_HANDOFF_REQUIRE_THP=1",
        root / "tools/preallocated_ram_probe.c",
        "-o",
        out / "allocation-probe",
    ]
)
run(
    [
        cc,
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        root / "tools",
        root / "tools/backing_allocation_probe.c",
        "-o",
        out / "pressure-check",
    ]
)
# Reuse guest-only bootstrap; no host commands are included in its ADB service.
import sys, shlex

sys.path.insert(0, str(root / "tools"))
from build_android_gpu_probe import early_command

(out / "android_guest_setup.generated.h").write_text(
    "static const char android_setup_service[] = "
    + json.dumps(
        "exec:sh -c "
        + shlex.quote(
            (root / "native/android_guest_setup.sh").read_text().replace("402653184", "1073741824")
        )
    )
    + ";\n"
)
(out / "android_gpu_early.generated.h").write_text(
    "static const char console_swap_command[] = "
    + json.dumps(
        early_command(
            (root / "native/android_guest_setup.sh").read_text().replace("402653184", "1073741824")
            + "\n"
            + (root / "tools/android_gpu_early_setup.sh").read_text()
        )
    )
    + ";\n"
)
shutil.copyfile(
    root / "app/build/native/owned-display/libplb_owned_display_backend.so",
    out / "libplb_owned_display_backend.so",
)
for name, (path, _) in expected_libraries.items():
    shutil.copyfile(path, out / name)
# Build fixed guest networking payloads; no host TUN/routes/VPN configuration.
import sys

for script in ("build_android_network_agent.py", "build_android_network_trial.py"):
    run([sys.executable, root / "tools" / script, "--sdk", sdk])
network = root / "artifacts/android4g-gpu/network-agent"
run(
    [
        cc,
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        network,
        "-DPLB_ANDROID17_NETWORK_APP=1",
        "-DPLB_ANDROID_ADB_TRACE=1",
        "-DPLB_ANDROID_ADB_TIMEOUT_SECONDS=20",
        "-DPLB_CID=39821",
        "-DPLB_PROXY_PORT=50442",
        '-DPLB_PROXY_LOG="/data/user/0/net.ryuya_dev.protectedlinux/files/android-network-proxy.log"',
        root / "native/android_network_controller.c",
        root / "native/network_proxy.c",
        "-o",
        out / "network-controller",
    ]
)
run(
    [
        cc,
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        network,
        "-I",
        root / "native",
        root / "tools/owned_display_probe/network_check_guest.c",
        "-o",
        out / "network-check",
    ]
)
files = {
    dest + "/" + p.name: digest(p)
    for p in out.iterdir()
    if p.name
    in [
        "guest.Image",
        "adopt-vmm",
        "launch-vmm",
        "libplb-vmm-entry.so",
        "owned-vmm.jar",
        "allocation-probe",
        "pressure-check",
        "network-controller",
        "network-check",
    ]
}
files[profile + "/android-lowram.cpio"] = digest(ramdisk)
for name in [
    "libplb_vmm_android17.so",
    "libgfxstream_backend.so",
    "libplb_owned_display_backend.so",
]:
    p = out / name
    files[dest + "/" + name] = digest(p)
(out / "pins.generated.h").write_text(
    "#define PLB_VIEWER_UID app_uid()\nstatic const struct { const char *path, *sha256; } pins[] = {\n"
    + "".join("    {" + json.dumps(p) + ", " + json.dumps(h) + "},\n" for p, h in files.items())
    + "};\n"
)
run(
    [
        cc,
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pthread",
        "-I",
        out,
        "-I",
        root / "tools",
        "-I",
        root / "native",
        "-DPLB_ANDROID17_APP_SESSION=1",
        root / "tools/owned_display_probe/vm_controller.c",
        "-o",
        out / "vm-controller",
    ]
)
(out / "build.json").write_text(
    json.dumps(
        {
            "runtime_files": files,
            "configured_ram_mib": 1024,
            "vulkan_execution": False,
            "guest_profile": profile,
        },
        indent=2,
    )
    + "\n"
)
# The optional runtime contains no OS disks. The pre-existing separate h17mfix
# profile is checked at launch; absent profiles remain a clear preparation error.
names = [
    "launch-vmm",
    "pressure-check",
    "guest.Image",
    "adopt-vmm",
    "allocation-probe",
    "libplb-vmm-entry.so",
    "owned-vmm.jar",
    "vm-controller",
    "network-controller",
    "network-check",
    "libplb_vmm_android17.so",
    "libgfxstream_backend.so",
    "libplb_owned_display_backend.so",
]
# Pin the guest ramdisk in its existing profile, never overwrite it.
(out / "android17_install.generated.h").write_text(
    "static const struct {const char *name,*sha256;} runtime_items[] = {\n"
    + "".join(" {" + json.dumps(n) + "," + json.dumps(digest(out / n)) + "},\n" for n in names)
    + "};\n"
)
run(
    [
        cc,
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        out,
        "-I",
        root / "native",
        root / "native/android17_runtime_install.c",
        "-o",
        out / "install",
    ]
)
a.assets.mkdir(parents=True, exist_ok=True)
for n in names + ["install"]:
    shutil.copyfile(out / n, a.assets / ("android17-app-" + n))
(a.assets / "android17-app-files.txt").write_text("\n".join(names + ["install"]) + "\n")
print("ANDROID17_APP_RUNTIME_BUILD_PASS")
