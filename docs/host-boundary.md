# Host Android boundary

This is an explicit device-owner constraint, reaffirmed on 2026-10-18. It applies to implementation, diagnostics, troubleshooting and release instructions.

The following are prohibited:

- Bootloader unlock (BLU).
- Writes to `boot`, `init_boot`, `vendor_boot`, `vbmeta`, `dtbo`, or any other host boot partition.
- Host kernel replacement or patches.
- Addition or loading of host kernel modules.
- Disabling SELinux, switching it to Permissive, or modifying its policy.
- Android reboot, soft reboot, recovery entry or recovery operations.
- Changes to AVB, dm-verity or Gunyah protection mechanisms.
- Changes to Android system regions, including system/vendor/product.

Maintain the locked bootloader and the existing temporary-root method. Do not use global mounts/remounts, host network/routes/firewall/DNS changes, global memory compaction/cache drops/sysctls, or virtualization property changes as workarounds. `su -M` is limited to the launched process's namespace.

Guest-only ARM64 kernels, modules, initramfs, DTB, rootfs and independent virtual disks may be built/added/changed **only in this app's private data**. Do not overwrite the installed Microdroid guest-kernel file. Keep that mode and the existing Debian disk available. Android guests must have separate disks and configuration. Guest changes still carry VMM/driver/hypervisor defect risk; host failure is not assumed impossible.

Only terminate tracked processes belonging to this app's VM or its bounded maintenance/probe. Never kill unrelated VM processes. If an operation requires a prohibited host change, report that limitation and leave the feature unavailable.
