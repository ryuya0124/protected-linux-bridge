# Device constraints

The user uses temporary root on a locked-bootloader Samsung device.

## Host Android: prohibited

- Never unlock the bootloader (BLU), write boot/init_boot/vendor_boot/vbmeta/dtbo or any other Android system/vendor/product or boot-related partition, replace/patch the host kernel, or add/load host kernel modules.
- Never change the root method, disable SELinux, make it Permissive, modify its policy, alter AVB/dm-verity/Gunyah protection mechanisms, or change global virtualization properties.
- Never reboot/soft-reboot Android or enter recovery.
- Do not mount/remount host global filesystems. `su -M` only changes the launched process's mount namespace.
- Test only on the explicitly selected Galaxy. Do not manipulate the attached Pixel or unrelated VMs.

## Guest-only kernels: explicitly permitted by the user, 2026-10-18

- Building, downloading, adding and selecting ARM64 guest kernels and guest-only modules, initramfs, DTB and rootfs is allowed.
- Store installed guest kernels and related files in this application's private data directory. Never overwrite the preinstalled `/apex/com.android.virt/etc/fs/microdroid_kernel` or any Android boot file.
- Retain the existing Microdroid-kernel mode and current Debian persistent disk. Custom kernels must use a separate selectable VM configuration and a reliable rollback path.
- First investigate compatibility with the installed Gunyah/crosvm. Initial boot probes must not attach the existing Debian disk.
- Stop only tracked, owned VM processes; never terminate Android or unrelated VMs. Guest-kernel changes can still expose host/hypervisor defects: risk is not zero.
- `native/guest_init.c` builds guest user-space `/init`, independently of any kernel build.

Limit Xposed scope to `com.android.virtualization.terminal`, API 102.
Private APKs, device identifiers, console traces, OS images, built kernel artifacts and signing keys must not be committed.

## Guest storage operations: updated request, 2026-10-18

- The user now explicitly requests shrinking as well as common VM size-management features; the earlier expansion-only preference is superseded.
- Never truncate an active filesystem or mutate the sole original as a trial. Shrink/compaction experiments must work on a verified offline copy and preserve a recoverable original.
- Keep virtual capacity, physical host allocation and guest filesystem usage distinct in the UI and validation.
- Android/LineageOS guests are requested as separate VM profiles, not replacements for the host Android or the existing Debian profile.
- Watch Mac storage. Avoid a full AOSP/LineageOS source checkout; reuse existing SDK images and bounded caches where possible. Do not remove unrelated project data.

## Readable source

- The user explicitly requests every source file to be human-readable. Keep first-party Java, C, Python, Gradle, XML, shell and web code expanded and consistently formatted. No hand-minified code.
- Use the checked-in formatting configuration and `docs/code-style.md`; retain upstream dependency/wrapper source unchanged.
- Put test C in formatted `tests/fixtures/` files rather than compressed Python string literals. Explain ownership, offline mutation and recovery invariants.

## Narrow runtime THP exception: explicit approval, 2026-10-19

The user explicitly approved temporary changes ONLY to these three host sysfs
settings, from `never` to `advise`, during bounded developer diagnostics:

- `/sys/kernel/mm/transparent_hugepage/hugepages-16kB/shmem_enabled`
- `/sys/kernel/mm/transparent_hugepage/hugepages-32kB/shmem_enabled`
- `/sys/kernel/mm/transparent_hugepage/hugepages-64kB/shmem_enabled`

First verify useful larger folios in a 16 MiB owned allocation; only then proceed
to diagnostic VMs. Use the prepared independent restoration guardian, restore
all three original values after each trial, and verify restoration and unchanged
host boundary. Each trial is limited to 40 seconds. This does not authorize any
other host/global setting, defrag/compaction/cache-drop write, anonymous THP
setting, kernel/module/partition/SELinux/protection change, or Android restart.
Keep all existing Debian disks and unrelated VMs protected as above. The released
APK must not silently adopt this global policy change.

## Developer diagnostic RAM cap: user update, 2026-10-19

- The current user limit is **6 GiB maximum**, including explicitly allocated
  probe RAM, its resident allocation target and reserved execution headroom.
- The removed 8 GiB pressure mode must be rejected before allocating anything.
- Keep memory-probe configured RAM at or below this cap and continue to serialize
  owned VM/allocation tests through the common owner lock.
- The allocation helper now stops starting batches below 1536 MiB MemAvailable;
  the older 512 MiB floor describes historical trials only. Do not lower it to
  bypass a refusal or change another process's OOM protection.

## Explicit Android 1.5 GiB app selection: requested 2026-10-19

The user asked to integrate the successful 1.5 GiB Android configuration and
update the app/GitHub records. Keep it a separate, explicitly selected profile;
show the host-wide three-key change and RAM pressure before each launch. Keep
512 MiB available without that policy/preparation. Do not persist or silently
auto-enable the host policy, broaden the three-key exception, remove the
40-second independent restoration guardian or weaken exact platform/VMM pins.
The startup RAM preparation remains within the 6 GiB cap and existing floor.
The owned root controller may leave its own launching app's cached-app cgroup
for the existing unified root cgroup, so its guardian cannot be frozen with the
UI. Never move another process or change freezer/CPU policies or directories.

## Host page-cache warning: observed 2026-10-19

A 3072/2048 MiB reverse-middle refault trial produced 242 new host
`BUG: Bad page cache` / `still mapped when deleted` reports, through
`shmem_fallocate` and `filemap_unaccount_folio`. Stop new VM/allocation-pressure
trials in this affected host boot. Do not clear taint, restart Android, or bypass
health guards to continue. The mapped-hole-punch refault helper is retired and
returns ENOTSUP; do not restore it as a fallback. The app now uses prefault-only
admission, without that helper. BAD_PAGE/OOPS taint or unreadable health state
refuses new diagnostic load and the guarded large Android profile before policy
changes. Expected root O/W taints alone are not BAD_PAGE. No clean-session
boot validation of the changed app path is claimed. Preserve the old result
records as historical evidence, and record the failed large trials and warning
separately; a 1792 MiB write marker is not successful full readback.


## Experimental 1792 MiB / 1536 MiB GPU requests, 2026-10-19

Keep the app's 1792 MiB selection explicitly unverified and separately stored.
A historical 1792 MiB write-progress marker is not a successful RAM profile.
The requested 1536 MiB GLES diagnostic must use a separate disk profile and
prefault-only backing, retaining exact host/VMM/library pins and the common lock.
These requests do not authorize bypassing the affected-boot health guard, loading
a module, broadening host policy writes or rebooting Android. No additional VM
or pressure trial is run in the current BAD_PAGE-tainted boot.


## Explicit single-trial exception, user approval after preview.7, 2026-10-19

The user explicitly accepted the risk and authorized **one** 1536 MiB GLES
trial in the current BAD_PAGE-marked boot. Only the separate developer artifact
may use the boot/time-bound, inherited-FD, O_EXCL single-use exception. Keep the
normal APK and diagnostics fail-closed. Never clear taint, restore hole punching,
retry a failed VMM automatically, broaden THP settings, reboot Android, modify
a host kernel/partition/protection, or touch another VM. Preserve existing disks
and use a separately copied GPU profile. Monitor new kernel reports and stop only
the owned trial on a new failure. A consumed trial needs new explicit user approval.

The exception has now been consumed: prefault-only backing admission exited 89,
with 25248/352256 middle pages carrying THP flags (7.17%), before guest boot.
Both policy restorers succeeded and original disk hashes remained unchanged.
Do not delete the spent marker, regenerate an approval window or launch another
affected-boot load without new explicit authorization. GPU drawing is unverified.
