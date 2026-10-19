#!/usr/bin/env python3
"""Build a separate Android16 diagnostic VMM; never touch or start an Android host.

Supply shallow clones of the two pinned public repositories and read-only copies
of the selected host's three link libraries. Only source copies under artifacts/
may be patched. Output, Cargo downloads and intermediates stay under artifacts/.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "artifacts"
VMM_REVISION = "8a4abfbaea5567de24e49cc43874c820098987d0"
MINIJAIL_REVISION = "c6fe710936b84753b99e6d0e9ea634109272f307"
LIBRARIES = ("libminijail.so", "libcap.so", "libprocessgroup.so")


def artifact_path(path):
    resolved = path.resolve()
    if not resolved.is_relative_to(ARTIFACTS.resolve()) or resolved == ARTIFACTS.resolve():
        raise ValueError("Use a dedicated path below this project's ignored artifacts/")
    return resolved


def git(source, *arguments, check=True):
    return subprocess.run(
        ["git", "-C", str(source), *arguments], capture_output=True, check=check, text=True
    )


def prepare_source(source, minijail, android_gpu=False, android_surface=False):
    if git(source, "rev-parse", "HEAD").stdout.strip() != VMM_REVISION:
        raise ValueError("Unexpected crosvm source revision")
    if git(minijail, "rev-parse", "HEAD").stdout.strip() != MINIJAIL_REVISION:
        raise ValueError("Unexpected minijail source revision")
    if git(minijail, "status", "--porcelain").stdout:
        raise ValueError("Use an unchanged minijail checkout")
    allowed = {
        "Cargo.toml",
        "hypervisor/Cargo.toml",
        "hypervisor/src/gunyah/mod.rs",
        "hypervisor/src/gunyah/gunyah_sys/bindings.rs",
        "prebuilts/src/sys.rs",
        "vm_memory/Cargo.toml",
        "vm_memory/src/guest_memory.rs",
        "third_party/minijail",
        "rutabaga_gfx/src/gfxstream.rs",
        "gpu_display/build.rs",
        "gpu_display/src/lib.rs",
        "gpu_display/src/sys/linux.rs",
    }
    surface_patch = ROOT / "tools/crosvm_android_surface.patch"
    if android_surface:
        if not android_gpu:
            raise ValueError("Surface library requires the separate GPU build")
        allowed.update(
            {"src/main.rs", "rutabaga_gfx/src/lib.rs", "devices/src/virtio/gpu/virtio_gpu.rs"}
        )
    if not set(git(source, "diff", "--name-only", "HEAD").stdout.splitlines()) <= allowed:
        raise ValueError("Source has unrelated tracked modifications")
    # Remove only our exact prior Surface patch while checking the baseline
    # patches, then reapply it. Extra init context must not invalidate their
    # reverse checks or accidentally apply a baseline patch twice.
    if (
        android_surface
        and not git(
            source, "apply", "--reverse", "--check", str(surface_patch), check=False
        ).returncode
    ):
        git(source, "apply", "--reverse", str(surface_patch))
    patch = ROOT / "tools/crosvm_preallocated_ram.patch"
    if git(source, "apply", "--reverse", "--check", str(patch), check=False).returncode:
        git(source, "apply", "--check", str(patch))
        git(source, "apply", str(patch))
    headless_patch = ROOT / "tools/crosvm_android_headless.patch"
    if android_gpu:
        if git(
            source, "apply", "--reverse", "--check", str(headless_patch), check=False
        ).returncode:
            git(source, "apply", "--check", str(headless_patch))
            git(source, "apply", str(headless_patch))
        diagnostic_patch = ROOT / "tools/crosvm_gpu_diagnostics.patch"
        if git(
            source, "apply", "--reverse", "--check", str(diagnostic_patch), check=False
        ).returncode:
            git(source, "apply", "--check", str(diagnostic_patch))
            git(source, "apply", str(diagnostic_patch))
    elif any(
        (name.startswith("gpu_display/") or name == "rutabaga_gfx/src/gfxstream.rs")
        for name in git(source, "diff", "--name-only", "HEAD").stdout.splitlines()
    ):
        raise ValueError("Use a separate source checkout for the original non-GPU baseline")
    modules = {
        "tools/crosvm_preallocated_ram.rs": "vm_memory/src/guest_memory/plb_adopt.rs",
        "tools/crosvm_split_lend.rs": "hypervisor/src/gunyah/plb_lend.rs",
    }
    for name, relative in modules.items():
        module = ROOT / name
        destination = source / relative
        if destination.is_symlink() or (
            destination.exists() and destination.read_bytes() != module.read_bytes()
        ):
            raise ValueError("Unexpected existing diagnostic module")
        destination.write_bytes(module.read_bytes())
    link = source / "third_party/minijail"
    if not link.is_symlink():
        raise ValueError("Expected upstream minijail symlink; will not replace a directory")
    if link.resolve() != minijail:
        link.unlink()
        link.symlink_to(minijail)
    if android_surface:
        git(source, "apply", "--check", str(surface_patch))
        git(source, "apply", str(surface_patch))


def build(args):
    source = artifact_path(args.source)
    minijail = artifact_path(args.minijail)
    libraries = artifact_path(args.host_libraries)
    output = artifact_path(args.output)
    if shutil.disk_usage(ROOT).free < 22 * 1024**3:
        raise ValueError("Retain at least 20 GiB disk headroom throughout the build")
    for name in LIBRARIES:
        path = libraries / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 4 * 1024**2:
            raise ValueError("Missing or oversized captured host link library")
        header = path.read_bytes()[:20]
        if header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\xb7\x00":
            raise ValueError("Expected an AArch64 ELF link library")
    surface_library = getattr(args, "android_surface_library", False)
    prepare_source(source, minijail, args.android_gpu, surface_library)
    if args.android_gpu:
        gfxstream = libraries / "libgfxstream_backend.so"
        if (
            gfxstream.is_symlink()
            or not gfxstream.is_file()
            or hashlib.sha256(gfxstream.read_bytes()).hexdigest()
            != "41414d640a526f56ce9a151f786e965a6e1fe3769e201790e0e58e7a31ff87da"
        ):
            raise ValueError("Unexpected captured gfxstream renderer")
    output.mkdir(mode=0o700, parents=True, exist_ok=True)
    pkgconfig = output / "pkgconfig"
    pkgconfig.mkdir(exist_ok=True)
    (pkgconfig / "libminijail.pc").write_text(
        "Name: libminijail\nDescription: Captured Android16 link library\nVersion: 0.0\n"
        f"Libs: -L{libraries} -lminijail\nCflags: -I{minijail}\n"
    )
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    ndk = args.sdk.resolve() / "ndk/28.2.13676358/toolchains/llvm/prebuilt" / host
    rust = args.rust_bin.resolve()
    environment = os.environ.copy()
    environment.update(
        {
            "CARGO_HOME": str(output / "cargo-home"),
            "CARGO_TARGET_DIR": str(output / "target"),
            "RUSTC": str(rust / "rustc"),
            "RUSTDOC": str(rust / "rustdoc"),
            "CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER": str(
                ndk / "bin/aarch64-linux-android35-clang"
            ),
            "CC_aarch64_linux_android": str(ndk / "bin/aarch64-linux-android35-clang"),
            "AR_aarch64_linux_android": str(ndk / "bin/llvm-ar"),
            "PKG_CONFIG_PATH_aarch64_linux_android": str(pkgconfig),
            "PKG_CONFIG_ALLOW_CROSS": "1",
            "LIBCLANG_PATH": str(args.llvm_library.resolve()),
            "BINDGEN_EXTRA_CLANG_ARGS": f"--sysroot={ndk}/sysroot -I{ndk}/sysroot/usr/include/aarch64-linux-android",
            "MINIJAIL_BINDGEN_TARGET": "aarch64-linux-android35",
            "CARGO_BUILD_JOBS": "4",
            "CARGO_TARGET_AARCH64_LINUX_ANDROID_RUSTFLAGS": "-C link-arg=-Wl,-rpath,/apex/com.android.virt/lib64",
        }
    )
    features = ["gunyah", "plb-preallocated-ram"]
    if args.split_ram:
        features.append("plb-split-ram")
    if args.android_gpu:
        features += ["gpu", "gfxstream", "qcow", "android-sparse"]
        environment["GFXSTREAM_PATH"] = str(libraries)
    command = [
        str(rust / "cargo"),
        "build",
        "--locked",
        "--release",
        "--target",
        "aarch64-linux-android",
        "--no-default-features",
        "--features",
        ",".join(features),
    ]
    command += ["--lib"] if surface_library else ["--bin", "crosvm"]
    log_path = output / ("surface-build.log" if surface_library else "build.log")
    with log_path.open("wb") as log:
        process = subprocess.Popen(
            command,
            cwd=source,
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            while process.poll() is None:
                if (
                    shutil.disk_usage(output).free < 21 * 1024**3
                    or log_path.stat().st_size > 64 * 1024**2
                ):
                    raise RuntimeError("Build exceeded disk/log budget")
                time.sleep(1)
            if process.returncode:
                raise RuntimeError(f"Cargo failed ({process.returncode}); inspect {log_path}")
        finally:
            if process.poll() is None:
                # Only the fresh Mac process group created by this builder.
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    binary = (
        output
        / "target/aarch64-linux-android/release"
        / ("libplb_crosvm_surface.so" if surface_library else "crosvm")
    )
    inputs = [
        ROOT / "tools/crosvm_preallocated_ram.rs",
        ROOT / "tools/crosvm_split_lend.rs",
        ROOT / "tools/crosvm_preallocated_ram.patch",
    ]
    if args.android_gpu:
        inputs.extend(
            [
                ROOT / "tools/crosvm_android_headless.patch",
                ROOT / "tools/crosvm_gpu_diagnostics.patch",
            ]
        )
    if surface_library:
        inputs.append(ROOT / "tools/crosvm_android_surface.patch")
    manifest = {
        "source_revision": VMM_REVISION,
        "minijail_revision": MINIJAIL_REVISION,
        "features": features,
        "rust_version": subprocess.check_output(
            [str(rust / "rustc"), "--version"], text=True
        ).strip(),
        ("surface_library_sha256" if surface_library else "vmm_sha256"): hashlib.sha256(
            binary.read_bytes()
        ).hexdigest(),
        "artifact_kind": "surface-library" if surface_library else "executable",
        "host_library_sha256": {
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (
                libraries / n
                for n in (*LIBRARIES, *(("libgfxstream_backend.so",) if args.android_gpu else ()))
            )
        },
        "patch_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        "build_starts_vm": False,
        "production_apk_changed": False,
    }
    (output / ("surface-vmm-build.json" if surface_library else "vmm-build.json")).write_text(
        json.dumps(manifest, indent=2) + "\n"
    )
    print(json.dumps(manifest, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--android-gpu",
        action="store_true",
        help="Separate qcow/gfxstream headless Android diagnostic",
    )
    parser.add_argument(
        "--android-surface-library",
        action="store_true",
        help="Experimental same-process JNI/Surface library, requires --android-gpu; never starts a VM",
    )
    parser.add_argument(
        "--split-ram", action="store_true", help="Separate 256 MiB registration comparison build"
    )
    for name in (
        "source",
        "minijail",
        "host-libraries",
        "sdk",
        "rust-bin",
        "llvm-library",
        "output",
    ):
        parser.add_argument("--" + name, type=Path, required=True)
    build(parser.parse_args())


if __name__ == "__main__":
    main()
