#!/system/bin/sh
# MIT. Explicit offline copy, protected by the shared VM owner lock.
# Refuse existing destinations and retain incomplete staging for inspection.
set -eu
umask 077
archive_suffix=
custom_guest=0
if [ "$#" != 0 ]; then
    [ "$#" = 1 ]
    case "$1" in
    --custom-guest) custom_guest=1 ;;
    --archive-normal-first) archive_suffix=normal-first ;;
    --archive-ascending) archive_suffix=ascending ;;
    --archive-medium-only) archive_suffix=medium-only ;;
    --archive-medium-aligned) archive_suffix=medium-aligned ;;
    --archive-all-aligned) archive_suffix=all-aligned ;;
    --archive-pipe-trace) archive_suffix=pipe-trace ;;
    *) exit 1 ;;
    esac
fi
[ "$(id -u)" = 0 ]
[ "$(getprop ro.product.model)" = SM-S948Q ]
files=/data/user/0/net.ryuya_dev.protectedlinux/files
owner=$files/vm-owner.lock
[ -f "$owner" ] && [ ! -L "$owner" ]
[ "$(stat -c '%u:%h:%a' "$owner")" = 0:1:600 ]
# The native copy supervisor passes its validated locked descriptor explicitly.
copy_fd=${PLB_ANDROID4G_COPY_FD:?Run android4g-profile-copy}
case "$copy_fd" in
'' | *[!0-9]*) exit 1 ;;
esac
[ "$(stat -Lc '%d:%i' "/proc/self/fd/$copy_fd")" = "$(stat -c '%d:%i' "$owner")" ]
source=$files/android-guests/cuttlefish17
target=$files/android-guests/cf17-gpu4096-adopted
if [ "$custom_guest" = 1 ]; then
    target=$files/android-guests/cf17-gpu4096-dma-sync
fi
pending=$target.preparing
if [ -n "$archive_suffix" ]; then
    archive=$target.failed-$archive_suffix
    [ -d "$target" ] && [ ! -L "$target" ]
    [ "$(stat -c '%u:%a' "$target")" = 0:700 ]
    [ ! -e "$archive" ] && [ ! -L "$archive" ]
    [ ! -e "$pending" ] && [ ! -L "$pending" ]
    mv "$target" "$archive"
    printf 'PLB_ANDROID4G_ARCHIVED trial=%s\n' "$archive_suffix"
fi
[ ! -e "$target" ] && [ ! -L "$target" ]
[ ! -e "$pending" ] && [ ! -L "$pending" ]
mkdir -m 700 "$pending"
for name in android-work.qcow2 android-misc.raw android-frp.raw; do
    input=$source/$name
    output=$pending/$name
    [ -f "$input" ] && [ ! -L "$input" ]
    [ "$(stat -c '%u:%h:%a' "$input")" = 0:1:600 ]
    before=$(sha256sum "$input" | cut -d ' ' -f 1)
    cp "$input" "$output"
    chmod 600 "$output"
    copied=$(sha256sum "$output" | cut -d ' ' -f 1)
    after=$(sha256sum "$input" | cut -d ' ' -f 1)
    [ "$before" = "$copied" ] && [ "$before" = "$after" ]
    sync "$output"
    printf 'PLB_ANDROID4G_COPY name=%s sha256=%s source_unchanged=1\n' "$name" "$after"
done
ramdisk=$files/kernel-probes/android4g-lowram.cpio
expected=ebf7141a7d287fdd877065109b5e68dfa9339a5a460c8d5795ff7355a36dd143
if [ "$custom_guest" = 1 ]; then
    ramdisk=$files/kernel-probes/android4g-custom-lowram.cpio
    identity=$files/kernel-probes/android4g-custom-ramdisk.sha256
    [ -f "$identity" ] && [ ! -L "$identity" ]
    [ "$(stat -c '%u:%h:%a' "$identity")" = 0:1:500 ]
    expected=$(cat "$identity")
    [ "${#expected}" = 64 ]
    case "$expected" in *[!0-9a-f]*) exit 1 ;; esac
fi
[ -f "$ramdisk" ] && [ ! -L "$ramdisk" ]
[ "$(sha256sum "$ramdisk" | cut -d ' ' -f 1)" = "$expected" ]
cp "$ramdisk" "$pending/android-lowram.cpio"
chmod 400 "$pending/android-lowram.cpio"
[ "$(sha256sum "$pending/android-lowram.cpio" | cut -d ' ' -f 1)" = "$expected" ]
sync "$pending/android-lowram.cpio"
mv "$pending" "$target"
printf 'PLB_ANDROID4G_PROFILE_READY\n'
