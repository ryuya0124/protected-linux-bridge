# Device constraints

The user uses temporary root on a locked-bootloader Samsung device.

- **Never update, replace, flash, or patch a kernel**, including a VM kernel.
- Use only `/apex/com.android.virt/etc/fs/microdroid_kernel`, already present on the device.
- Never write system, vendor, product, boot, vendor_boot, or other partitions.
- Never change root method, SELinux policy/enforcement, or global virtualization properties.
- Never reboot or soft-reboot without fresh explicit user permission.
- `native/guest_init.c` builds a guest user-space `/init`; it is not a kernel build.
- `su -M` changes the launched process's mount namespace only; do not mount/remount global filesystems.
- Limit Xposed scope to `com.android.virtualization.terminal`, API 102.
- Test only on the explicitly selected device. Do not manipulate the attached Pixel.
- Private APKs, OS images, console traces, device identifiers, and signing keys must not be committed.
