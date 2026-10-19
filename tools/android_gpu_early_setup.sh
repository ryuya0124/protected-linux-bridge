#!/system/bin/sh
# MIT. Appended to the standard guest-only bootstrap in the diagnostic build.
# The standard bootstrap verifies root, device/hardware/build, enables guest
# swap and aliases guest console nodes. No Android host setting is accessed.
set -eu
printf 'PLB_GPU_EARLY_SETUP_READY\n'
getprop ro.hardware.egl
getprop ro.hardware.vulkan
getprop debug.renderengine.backend
cat /proc/swaps
