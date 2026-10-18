# Guest-only Linux kernel

These recipes build an ARM64 **guest** kernel. They do not install or load anything into Android's host kernel. The owner explicitly allowed this on 2026-10-18; see [the compatibility plan](../docs/guest-kernel-plan.md). Stock Microdroid remains the default and its existing file must never be overwritten.

The first compatibility probe used `polygraphene/gunyah-on-sd-guide` v0.0.3, commit `7dbe3a9eafe28134f17b5a272e5461f35661c9bf`, release asset SHA256 `4b62b173dda4d01c3802b48d96062809d6ee31b07a0c2dc36f205218013d3438`. Its Linux 6.17.11 image booted a diskless initramfs and powered off normally through the installed Galaxy crosvm/Gunyah. It is an old compatibility specimen, not the new maintained default.

The new recipe targets kernel.org Linux 6.18.57 LTS. Download `linux-6.18.57.tar.xz` and `linux-6.18.57.tar.sign` from `https://cdn.kernel.org/pub/linux/kernel/v6.x/`. Place them in a private build cache. Source SHA256 is pinned in `build.sh`; the uncompressed archive's developer signature is also required, with release-key fingerprints checked against kernel.org's published keys. Obtain `microdroid_defconfig` from AOSP kernel/common tag `android-15.0.0_r0.81` (its decoded SHA256 is pinned too). `plb.config` adds guest devices/namespaces. There are no source patches.

Build the Dockerfile and run `build.sh CACHE` in its ARM64 Linux container, with this directory mounted read-only and the cache mounted writable. Cap CPU at 4 and memory at 4 GiB. Do not use privileged mode, Android devices, host Docker socket mounts or kernel install targets. The developer's build monitor stops its specifically named build container if Mac free space drops below 20 GiB or this build cache exceeds 6 GiB. Preserve the resulting Image, final `.config`, SHA256 and provenance; delete intermediate source/object trees after validation if space is needed. Do not prune other projects' Docker images/volumes.

Enabling virtio-net, TUN, USB/xHCI, DRM/virtio-GPU, sound and Binder makes guest drivers available. It does **not** create a host network, transfer a physical device, supply an Android HAL, or establish GPU/video acceleration. Each host/VMM connection still needs separate validation. No host firewall/routing, SELinux or protection change is part of this recipe.

Linux is GPL-2.0-only with its documented syscall exceptions; AOSP configuration licensing follows its upstream source. When distributing a built kernel, provide its exact corresponding source, final config and build recipe alongside it. Kernel build products are not committed to this repository.

## Rebuild command and storage guard

Use an ARM64 Docker runtime on the development machine. Cache files stay below the repository's ignored `artifacts/guest-kernel-probe/`. The developer's output used GCC14.2.0-19 / binutils2.44. A different toolchain may produce a different hash and is not automatically accepted by the app.

```sh
docker build -t plb-guest-kernel-builder:6.18 -f kernels/Dockerfile kernels
# Mount only the recipe and this task's cache; do not mount Android devices.
docker run --rm --name plb-guest-kernel-build --cpus 4 --memory 4g \
  --cap-drop ALL --security-opt no-new-privileges \
  --user "$(id -u):$(id -g)" \
  -v "$PWD/kernels:/work/kernels:ro" \
  -v "$PWD/artifacts/guest-kernel-probe:/work/artifacts/guest-kernel-probe" \
  plb-guest-kernel-builder:6.18 \
  sh /work/kernels/build.sh /work/artifacts/guest-kernel-probe
```

Check Mac free space before starting and during the build. Stop only this named build if it falls below20GiB or this cache exceeds6GiB. After saving Image, config, signature verification and hashes, the extracted `linux-6.18.57/` and `output/` are expendable. Keep the source archive/signature for verification and corresponding-source distribution. This task removed those intermediate trees and its own builder image; it did not prune other projects' Docker data.
