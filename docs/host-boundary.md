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

## Narrow, explicit memory exception

On 2026-10-19 the owner approved bounded changes only to the host's 16/32/64 KiB
`shmem_enabled` nodes, `never` → `advise` with restoration, then requested the
successful 1.5 GiB Android configuration in the app. Preview.5 displays this
host-wide effect and RAM preparation before each explicit launch. Normal restore
is at ten seconds; an independent guardian attempts restore on controller loss or
a forty-second deadline. No other memory policy or prohibited host change is
authorized. See [implementation and measured limits](android-large-profile.md).
The root controller may move only itself out of the launching app's cached-app
freezer cgroup; it does not change freezer policies or any other process.

After preview.7, the owner separately accepted the risk and allowed **one**
1536 MiB GLES developer trial in the affected boot. Only a distinct boot/time-
and inherited-FD-bound artifact could claim the root-private `O_EXCL` counter.
The counter is now consumed. That trial stopped at the unchanged prefault-only
backing gate, exit 89, before guest boot. Both restoration acknowledgements and
unchanged original-disk hashes were verified. Normal builds still reject the
affected boot. No automatic retry or fresh approval is inferred from this
exception. [Exact result and limits](android-gpu1536-research.md).
