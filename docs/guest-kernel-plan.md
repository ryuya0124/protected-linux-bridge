# Guest kernel policy and compatibility investigation

Policy updated by the device owner on 2026-10-18. This supersedes the earlier blanket ban on guest kernel changes.

## Boundaries

Host Android remains immutable: locked bootloader, existing temporary root, host kernel and kernel modules, Android boot/system partitions, AVB/dm-verity, SELinux and Gunyah protections are unchanged. Android reboot/recovery is prohibited. Guest kernel changes do not imply modifying host AVB. They can nevertheless trigger defects in crosvm, the host driver or hypervisor; host stability cannot be guaranteed merely by using a separate guest kernel.

Custom ARM64 guest kernels, guest modules, initramfs/DTB/rootfs may be built or obtained, and installed only in this app's private data area. Preserve the existing preinstalled Microdroid kernel mode as the default and retain the current Debian filesystem.

## Required sequence

1. Inspect the installed Gunyah/crosvm interfaces and a kernel source compatible with their guest boot ABI.
2. Build or obtain a reproducible kernel with pinned provenance and review its guest support. Do not load host modules or weaken host protection to accommodate it.
3. Probe a separately named, bounded minimal VM with serial output and a small initramfs; do not attach the existing Debian disk, network or physical devices.
4. Only after successful probes, add explicit kernel-profile selection, validation and rollback. A failed profile must not silently modify the default profile or disk.
5. Use a separate writable disk copy for initial Debian compatibility tests; never attach the same writable disk to two VMs.

## Android guests

Android 15/16 support remains an investigation, not an implemented capability. A kernel alone does not supply Android boot/root images, fstab/partitions, Binder, vendor HALs, graphics/composer, input, SELinux policy or suitable virtual devices. Validate these separately before describing Android guests as supported.

## Current status

The stock Microdroid mode boots Debian. Diskless custom-kernel boot and an isolated Debian-copy boot have been verified; workload stability remains limited (measurements below). Networking, direct USB, DRM/Vulkan and video acceleration must be re-evaluated against an actually bootable guest kernel and devices exposed by the installed VMM; enabling CONFIG options alone is insufficient.

## 2026-10-18 measurements

- Release Linux 6.17.11: diskless ARM64 initramfs boot, guest poweroff, crosvm exit0.
- Own Linux 6.18.57-plb1: upstream source SHA256 and Greg Kroah-Hartman's developer signature verified; built in a bounded ARM64 Linux container. Diskless boot/poweroff passed. Final config and source/build provenance are in `kernels/`.
- A byte-for-byte verified, separate copy of the stopped Debian qcow2 boots with the new kernel, actual systemd PID1 and zero failed units in the initial sample. Guest `/dev/net/tun` exists. A temporary guest-only binderfs mount plus BINDER_CTL_ADD/BINDER_VERSION returned protocol8. No Android guest was booted.
- The original disk SHA256 stayed unchanged while the copy booted and while the copy's VM crashed. A file created in the experimental copy does not imply it exists in the original profile.
- Heavy GUI/OpenGL activity produced Gunyah exit reason3. Host diagnostic output includes `Failed to map guest address ...: 10` and `Failed to provide folio ...: -12`. The published Gunyah UAPI calls reason3 PAGE_FAULT; the installed crosvm exits on an unhandled reason. This is a memory-provisioning failure, not evidence that all guest drivers work reliably.
- VM-local `--hugepages` / `--lock-guest-memory-dontneed` did not eliminate that workload failure; those experimental flags were removed. No global memory compaction, cache drop, host sysctl, kernel patch or protection change was used.
- The experimental profile starts conservatively (512 MiB, 1 CPU, CPU graphics); it remains opt-in. Do not label it production-stable based on the successful boot. The stock profile stays available.
- Both profiles use one root-private ownership lock. A manager and an offline disk copy cannot run concurrently. Copying never opens the original writable. Ambiguous/incomplete output is not used as an existing profile.

References: [Gunyah UAPI](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/blob/8e66bb68fe6735e93ef5dd59c184cf6341b0098d/include/uapi/linux/gunyah.h), [memory provisioning implementation](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/blob/8e66bb68fe6735e93ef5dd59c184cf6341b0098d/drivers/virt/gunyah/vm_mgr_mem.c), [crosvm Gunyah backend](https://chromium.googlesource.com/crosvm/crosvm/+/refs/heads/main/hypervisor/src/gunyah/mod.rs).
