#!/usr/bin/env python3
"""Build a separate 4 GiB adopted-RAM Android GLES probe; never install/start it."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import shlex
import subprocess
import stat

from android_ramdisk import entries

from build_android_gpu_probe import gles_ramdisk, early_command
from build_guest_gpu_payload import build as build_payload

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def custom_ramdisk(original, record):
    """Keep guest init/AVB/properties, overriding only the module load list.

    This kernel has the needed virtual devices built in and disallows modules;
    never try to load the retained original guest's ABI-incompatible .ko files.
    """
    contents = {name: (mode, payload) for name, mode, payload in entries(original)}
    module_list = contents.get("lib/modules/modules.load")
    if module_list is None or not stat.S_ISREG(module_list[0]):
        raise ValueError("Expected the pinned first-stage guest module list")
    marker = (
        json.dumps(
            {
                "guest_only": True,
                "kernel_sha256": record["kernel_sha256"],
                "kernel_release": record["kernel_release"],
            },
            indent=2,
        ).encode()
        + b"\n"
    )
    extra = bytearray()
    for inode, name, mode, payload in (
        (100010, "lib/modules/modules.load", stat.S_IFREG | 0o644, b""),
        (100011, "plb-guest-kernel.json", stat.S_IFREG | 0o400, marker),
        (100012, "TRAILER!!!", 0, b""),
    ):
        encoded = name.encode() + b"\0"
        fields = [inode, mode, 0, 0, 1, 0, len(payload), 0, 0, 0, 0, len(encoded), 0]
        extra.extend(b"070701" + "".join(f"{v:08x}" for v in fields).encode() + encoded)
        extra.extend(b"\0" * (-len(extra) % 4))
        extra.extend(payload)
        extra.extend(b"\0" * (-len(extra) % 4))
    result = original + extra
    final = {name: payload for name, mode, payload in entries(result)}
    if final["lib/modules/modules.load"] or final["plb-guest-kernel.json"] != marker:
        raise ValueError("Guest override archive did not round trip")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--vmm", type=Path, required=True)
    parser.add_argument("--build-manifest", type=Path, required=True)
    parser.add_argument("--guest-render-apk", type=Path, required=True)
    parser.add_argument("--minimum-folio-kib", type=int, choices=(16, 128), default=16)
    parser.add_argument(
        "--preparation-order", choices=("ascending", "normal-first"), default="ascending"
    )
    parser.add_argument("--transport", choices=("pipe", "asg"), default="pipe")
    parser.add_argument("--swiotlb-mib", type=int, choices=(32, 128), default=32)
    parser.add_argument(
        "--buffer-capset",
        action="store_true",
        help="Expose gfxstream format/Vulkan caps (also initializes the Vulkan backend)",
    )
    parser.add_argument("--loader-compat", action="store_true")
    parser.add_argument("--disable-renderer-vulkan", action="store_true")
    parser.add_argument("--guest-kernel", type=Path)
    parser.add_argument("--guest-kernel-manifest", type=Path)
    args = parser.parse_args()
    if args.loader_compat and not args.buffer_capset:
        parser.error("The isolated Vulkan loader bridge requires the buffer-capset comparison")
    if args.disable_renderer_vulkan and (not args.buffer_capset or args.loader_compat):
        parser.error("The renderer Vulkan override requires format caps and no loader bridge")
    custom = args.guest_kernel is not None
    if custom != (args.guest_kernel_manifest is not None):
        parser.error("Select both the separate guest Image and its completed build manifest")
    guest_record = None
    if custom:
        guest_record = json.loads(args.guest_kernel_manifest.read_text())
        config = args.guest_kernel_manifest.parent / "output/.config"
        required = (
            "DRM_VIRTIO_GPU",
            "VIRTIO_BLK",
            "VIRTIO_PCI",
            "VIRTIO_CONSOLE",
            "VIRTIO_VSOCKETS",
            "F2FS_FS",
            "DM_DEFAULT_KEY",
            "DM_VERITY",
            "ANDROID_BINDER_IPC",
            "SECURITY_SELINUX",
        )
        if (
            guest_record.get("kernel_sha256") != digest(args.guest_kernel)
            or guest_record.get("source_commit") != "3ec022196c4e9d5c1434599cdda63f622dd6f586"
            or guest_record.get("upstream_fix") != "598c1c3e895590f845e04455d5580ea28ffde666"
            or guest_record.get("kernel_release")
            != (
                "6.12.74-plb-android-gpu2"
                if guest_record.get("system_heap")
                else "6.12.74-plb-android-gpu1"
            )
            or (
                guest_record.get("system_heap")
                and "CONFIG_DMABUF_HEAPS_SYSTEM=y\n" not in config.read_text()
            )
            or guest_record.get("config_sha256") != digest(config)
            or guest_record.get("patch_sha256")
            != digest(ROOT / "kernels/android-gpu/guest-bound-dma-sync.patch")
            or guest_record.get("guest_only") is not True
            or guest_record.get("android_host_modified") is not False
            or guest_record.get("vm_started_by_build") is not False
            or "# CONFIG_MODULES is not set\n" not in config.read_text()
            or any("CONFIG_" + option + "=y\n" not in config.read_text() for option in required)
        ):
            parser.error("Custom guest Image/config/provenance does not match the guarded build")
    if args.swiotlb_mib != 32 and (not custom or not guest_record.get("system_heap")):
        parser.error("The larger guest DMA pool requires the isolated system-heap guest")
    if shutil.disk_usage(ROOT).free < 21 * 1024**3:
        parser.error("Retain 20 GiB Mac storage headroom")
    manifest = json.loads(args.build_manifest.read_text())
    expected = {"gunyah", "plb-preallocated-ram", "gpu", "gfxstream", "qcow", "android-sparse"}
    if (
        manifest.get("vmm_sha256") != digest(args.vmm)
        or set(manifest.get("features", [])) != expected
        or manifest.get("host_library_sha256", {}).get("libgfxstream_backend.so")
        != "41414d640a526f56ce9a151f786e965a6e1fe3769e201790e0e58e7a31ff87da"
        or manifest.get("source_revision") != "8a4abfbaea5567de24e49cc43874c820098987d0"
        or manifest.get("minijail_revision") != "c6fe710936b84753b99e6d0e9ea634109272f307"
        or any(
            manifest.get("patch_sha256", {}).get(name) != digest(ROOT / "tools" / name)
            for name in (
                "crosvm_preallocated_ram.rs",
                "crosvm_split_lend.rs",
                "crosvm_preallocated_ram.patch",
                "crosvm_android_headless.patch",
                "crosvm_gpu_diagnostics.patch",
            )
        )
    ):
        parser.error("Use the pinned separate Android GPU VMM build")
    out = ROOT / "artifacts/android4g-gpu" / ("probe-build-custom" if custom else "probe-build")
    label = "android4g-custom" if custom else "android4g"
    out.mkdir(mode=0o700, parents=True, exist_ok=True)
    ramdisk = out / "android-lowram.cpio"
    ramdisk.write_bytes(
        gles_ramdisk(
            (ROOT / "artifacts/android-display/cuttlefish17/android-lowram.cpio").read_bytes()
        )
    )
    if custom:
        ramdisk.write_bytes(custom_ramdisk(ramdisk.read_bytes(), guest_record))
    pins = {
        "VMM": digest(args.vmm),
        "KERNEL": (
            digest(args.guest_kernel)
            if custom
            else "f722bdd809bfd3485917a564b053b8a6d4938408734091f420c3297a8e3b20c8"
        ),
        "INITRAMFS": digest(ramdisk),
    }
    host = "darwin-x86_64" if platform.system() == "Darwin" else "linux-x86_64"
    compiler = (
        args.sdk
        / "ndk/28.2.13676358/toolchains/llvm/prebuilt"
        / host
        / "bin/aarch64-linux-android35-clang"
    )
    flags = [str(compiler), "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(out)]
    if args.swiotlb_mib == 128:
        flags.append("-DPLB_ANDROID4G_LARGER_DMA_POOL=1")
    if args.disable_renderer_vulkan:
        flags.append("-DPLB_ANDROID4G_DISABLE_RENDERER_VULKAN=1")
    if args.buffer_capset:
        flags.append("-DPLB_ANDROID4G_BUFFER_CAPSET=1")
    if args.preparation_order == "normal-first":
        flags.append("-DPLB_ANDROID4G_NORMAL_FIRST=1")
    if args.transport == "asg":
        flags.append("-DPLB_ANDROID4G_ASG=1")
    if custom:
        flags.append("-DPLB_ANDROID4G_CUSTOM_GUEST=1")
    compatibility = None
    if args.loader_compat:
        flags.append("-DPLB_ANDROID4G_LOADER_COMPAT=1")
        compatibility = out / "android4g-loader-compat.so"
        subprocess.run(
            flags
            + [
                "-shared",
                "-fPIC",
                "-DPLB_GFXSTREAM_DIAGNOSTIC_LOG=1",
                str(ROOT / "tools/gfxstream_android_compat.c"),
                "-ldl",
                "-o",
                str(compatibility),
            ],
            check=True,
        )
        pins["COMPAT"] = digest(compatibility)
    (out / "preallocated_vm.generated.h").write_text(
        "/* Generated private artifact identities. */\n"
        + "\n".join(
            f'#define PLB_PREALLOCATED_{key}_SHA256 "{value}"' for key, value in pins.items()
        )
        + "\n"
    )
    proxy = out / (label + "-adopt-vmm")
    subprocess.run(
        flags
        + [
            "-DPLB_PREALLOCATED_ANDROID_PROXY=1",
            str(ROOT / "tools/preallocated_vm_probe.c"),
            "-o",
            str(proxy),
        ],
        check=True,
    )
    (out / "android4g.generated.h").write_text(
        f'#define PLB_ANDROID4G_VMM_SHA256 "{pins["VMM"]}"\n'
        f'#define PLB_ANDROID4G_PROXY_SHA256 "{digest(proxy)}"\n'
        f'#define PLB_ANDROID4G_KERNEL_SHA256 "{pins["KERNEL"]}"\n'
        f'#define PLB_ANDROID4G_INITRAMFS_SHA256 "{pins["INITRAMFS"]}"\n'
        + (f'#define PLB_ANDROID4G_COMPAT_SHA256 "{pins["COMPAT"]}"\n' if compatibility else "")
    )
    setup = (ROOT / "native/android_guest_setup.sh").read_text()
    (out / "android_guest_setup.generated.h").write_text(
        "static const char android_setup_service[] =\n"
        + json.dumps("exec:sh -c " + shlex.quote(setup))
        + ";\n"
    )
    (out / "android_gpu_early.generated.h").write_text(
        "static const char console_swap_command[] =\n"
        + json.dumps(early_command((ROOT / "tools/android_gpu_early_setup.sh").read_text()))
        + ";\n"
    )
    apk_sha = build_payload(args.guest_render_apk, args.sdk, out / "android_gpu_probe.generated.h")
    manager = out / (label + "-manager")
    subprocess.run(
        flags
        + [
            "-pthread",
            "-DPLB_ANDROID_ADOPTED_PROBE=1",
            "-DPLB_ANDROID_GFXSTREAM_PROBE=1",
            "-DPLB_ANDROID_GPU_RENDER_PROBE=1",
            str(ROOT / "tools/android_probe_runner.c"),
            "-o",
            str(manager),
        ],
        check=True,
    )
    controller = out / (label + "-policy")
    subprocess.run(
        flags
        + [
            "-DPLB_PREALLOCATED_VM_POLICY=1",
            "-DPLB_MEDIUM_SHMEM_PROPOSAL=1",
            *(["-DPLB_ANDROID4G_MEDIUM_ONLY=1"] if args.minimum_folio_kib == 128 else []),
            str(ROOT / "tools/small_shmem_policy_probe.c"),
            "-o",
            str(controller),
        ],
        check=True,
    )
    copy = out / "android4g-profile-copy"
    subprocess.run(
        flags + [str(ROOT / "tools/android4g_profile_copy.c"), "-o", str(copy)], check=True
    )
    diagnostics = out / "android4g-guest-diagnostics"
    subprocess.run(
        flags + [str(ROOT / "tools/android4g_guest_diagnostics.c"), "-o", str(diagnostics)],
        check=True,
    )
    extra_files = [compatibility] if compatibility else []
    if custom:
        identity = out / "android4g-custom-ramdisk.sha256"
        identity.write_text(pins["INITRAMFS"] + "\n")
        extra_files.append(identity)
    record = {
        "configured_memory_mib": 4096,
        "guest_swiotlb_mib": args.swiotlb_mib,
        "guest_dma_pool_carved_from_configured_ram": True,
        "custom_guest_kernel": custom,
        "guest_kernel_build": guest_record,
        "buffer_format_capset_exposed": args.buffer_capset,
        "vulkan_parameter_requested": False,
        "capset_mask_enables_vulkan_backend": args.buffer_capset,
        "android_loader_compatibility": args.loader_compat,
        "renderer_feature_override": "Vulkan:disabled" if args.disable_renderer_vulkan else None,
        "guest_graphics_transport": "virtio-gpu-" + args.transport,
        "advised_shmem_sizes_kib": (
            [16, 32, 64, 128, 256, 512] if args.minimum_folio_kib == 16 else [128, 256, 512]
        ),
        "backing_preparation_order": args.preparation_order,
        "backing_mapping_alignment_bytes": 2097152,
        "gpu_bar_bytes": 268435456,
        "combined_explicit_budget_limit_mib": 6144,
        "host_policy_max_ms": 40000,
        "vmm_pins": pins,
        "guest_render_apk_sha256": apk_sha,
        "files_sha256": {
            p.name: digest(p)
            for p in (
                proxy,
                manager,
                controller,
                ramdisk,
                copy,
                diagnostics,
                ROOT / "tools/prepare_android4g_profile.sh",
                *extra_files,
            )
        },
        "vm_started_by_build": False,
        "production_apk_changed": False,
    }
    (out / "build.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
