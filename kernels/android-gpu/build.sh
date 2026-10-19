#!/bin/sh
# GPL-2.0-only kernel build recipe. Build Image only; never install or flash.
set -eu
export LC_ALL=C KBUILD_BUILD_USER=plb KBUILD_BUILD_HOST=guest-builder
export KBUILD_BUILD_TIMESTAMP='Tue Oct 20 00:00:00 UTC 2026' KBUILD_BUILD_VERSION=1
source=/work/kernel_common-3ec022196c4e9d5c1434599cdda63f622dd6f586
output=/work/output
[ -f "$source/Makefile" ]
config_sha=$(sha256sum /work/base.config | cut -d ' ' -f 1)
case "$config_sha" in
4c065ae7247d878dc34438e61230fb4c3e4a9ce286cd585bf49db03dd7e69726) ;;
9829760cab00bedf092bc93c0dfc05fe7a78aab7ec7121df44ed446f38cac513) [ "${PLB_SYSTEM_HEAP:-0}" = 1 ] ;;
*) exit 1 ;;
esac
mkdir -p "$output"
cp /work/base.config "$output/.config"
cd "$source"
make ARCH=arm64 LLVM=-19 O="$output" olddefconfig
scripts/kconfig/merge_config.sh -m -O "$output" "$output/.config" /recipe/android.fragment
if [ "${PLB_SYSTEM_HEAP:-0}" = 1 ]; then
    scripts/kconfig/merge_config.sh -m -O "$output" "$output/.config" /recipe/system-heap.fragment
fi
make ARCH=arm64 LLVM=-19 O="$output" olddefconfig
for option in DRM_VIRTIO_GPU VIRTIO_BLK VIRTIO_PCI VIRTIO_CONSOLE VIRTIO_VSOCKETS F2FS_FS DM_DEFAULT_KEY DM_VERITY ANDROID_BINDER_IPC SECURITY_SELINUX; do
    grep -qx "CONFIG_${option}=y" "$output/.config"
done
grep -qx '# CONFIG_MODULES is not set' "$output/.config"
if [ "${PLB_SYSTEM_HEAP:-0}" = 1 ]; then
    grep -qx 'CONFIG_LOCALVERSION="-plb-android-gpu2"' "$output/.config"
    grep -qx 'CONFIG_DMABUF_HEAPS_SYSTEM=y' "$output/.config"
else
    grep -qx 'CONFIG_LOCALVERSION="-plb-android-gpu1"' "$output/.config"
fi
make ARCH=arm64 LLVM=-19 O="$output" -j4 Image
cp "$output/arch/arm64/boot/Image" /work/Image-android17-dma-sync
make -s ARCH=arm64 LLVM=-19 O="$output" kernelrelease >/work/kernel-release.txt
sha256sum /work/Image-android17-dma-sync
