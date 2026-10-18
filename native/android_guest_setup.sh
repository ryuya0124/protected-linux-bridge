#!/system/bin/sh
# MIT. Run only inside the pinned Android 17 Cuttlefish diagnostic guest.
# Changes are volatile guest /dev nodes and guest swap. No SELinux changes.
set -eu
[ "$(id -u)" = 0 ] || exit 90
[ "$(getprop ro.product.device)" = vsoc_arm64_only ] || exit 91
[ "$(getprop ro.hardware)" = cutf_cvm ] || exit 92
[ "$(getprop ro.build.version.incremental)" = 16373615 ] || exit 93
case "$(cat /proc/cmdline)" in *androidboot.hardware=cutf_cvm*) ;; *) exit 94 ;; esac

# This image enables swap only after boot completion. It needs it during boot.
if [ "$(cat /sys/block/zram0/disksize)" = 0 ]; then
    echo 402653184 >/sys/block/zram0/disksize
    mkswap /dev/block/zram0
    swapon /dev/block/zram0
fi

# Gunyah's available IRQ layout cannot accommodate 20 separate console devices.
# Route three real virtio consoles to the guest HAL's expected names instead.
# The normal guest file-context database supplies their existing SELinux types.
for pair in 18:0 19:1 10:3; do
    target=${pair%:*}
    source=${pair#*:}
    [ "$(cat /sys/class/tty/hvc$source/dev)" = "229:$source" ] || exit 95
    [ -c /dev/hvc$target ] || exit 96
    # ueventd pre-creates these nodes even when their port does not exist.
    case "$(stat -c '%t:%T' /dev/hvc$target)" in
    e5:12 | e5:13 | e5:a | e5:0 | e5:1 | e5:3) ;; *) exit 97 ;;
    esac
    rm /dev/hvc$target
    mknod /dev/hvc$target c 229 "$source"
    if [ "$target" = 10 ]; then
        chown hsm:hsm /dev/hvc$target
    else
        chown system:system /dev/hvc$target
    fi
    chmod 660 /dev/hvc$target
    restorecon /dev/hvc$target
done
printf 'PLB_ANDROID_GUEST_SETUP_READY\n'
