# Disk management

The selected Debian profile has its own qcow2 disk. Storage controls never address host partitions or attach the original Debian disk to an Android guest. Both kernel profiles remain separate.

## Retired development image

On 2026-10-20, the device's Android 16 guest profile (`android-guests/sdk36`)
was removed with explicit user authorization, releasing about 4.68 GiB of
allocated storage. Its system image matched the official SDK 36 ARM64 image;
no other inspected qcow2 image referenced that directory as a backing file.
Deletion used the production owner and maintenance locks, and a subsequent
check confirmed the directory was absent. Android 17 guest profiles and Debian
directories were retained, including Android 17 profiles originally created
on an Android 16 host. The frozen Android 16 source and release APK remain
available; this device cleanup did not change them.

## Available controls

| Control | Behavior |
| --- | --- |
| Capacity | Whole GiB from 1 to 128. Expand or shrink the guest filesystem and virtual disk together. |
| Reclaim unused space | Zero unused ext4 blocks on a working copy, then recreate a thin disk at the same virtual capacity. |
| Previous disk | Successful maintenance retains the complete former disk. It is included in Android allocation until deleted. |
| Restore | Atomically swap current/previous disks; both survive, so the swap can be reversed. |
| Delete recovery disk | Explicit confirmation; delete only the known previous disk while a current disk exists. |
| Cancel | Stop only the tracked maintenance guest, remove incomplete copies and retain the original. |
| Usage | Show virtual capacity, current/previous Android allocation and Android free space separately. A running guest also reports filesystem total/used/available. |

Thin allocation means the file consumes space as blocks are written. It is already enabled; it does not make the guest's advertised capacity automatically unlimited. Filesystem metadata, qcow2 metadata, deleted data, reserved blocks, recovery disks and the separate immutable base explain differences between displayed values. Current-disk allocation excludes the base and other profiles; it is not total app storage.

RAM, vCPUs, GUI resolution/frame-rate cap and lifetime are separate settings. The legacy resources capacity menu still schedules expansion at next boot; use Disk management for retained-backup/offline operations. A previous disk blocks another maintenance job until explicitly handled. Sparse shrink below guest usage/metadata requirements fails; do not interpret free space alone as a guaranteed minimum. No minimum-size guarantee, fixed/preallocated storage, automatic maximum expansion, arbitrary format import/export, multi-snapshot tree or online shrink is claimed.

## Offline algorithm and recovery

1. Gracefully stop this app's VM/manager. Acquire the shared nonblocking owner lock used by both profiles. Another manager/operation causes refusal.
2. Check fixed private paths and types/owners; reject symlinks, hardlinks, ambiguous recovery state and insufficient host free space. Reserve worst-case working plus final capacity plus 512 MiB before starting.
3. Original block is readonly. Create zero-filled thin working and final qcow2 images. A separately named bounded protected maintenance VM uses the existing stock Microdroid kernel and no network, vsock, GPU or physical devices.
4. Copy all original virtual bytes to the working disk, then read back and compare. Mount the original only readonly with `noload` inside the guest for Debian tools/libraries. Work/final devices stay unmounted.
5. Run `e2fsck -fp` on the work disk; exit0/1 accepted. Resize the work filesystem to the requested capacity, check its superblock and run fsck again. A dirty original's journal is recovered only in the working copy.
6. Run the separately licensed pinned `zerofree` on unmounted work. Copy target-length bytes to the fresh final disk, skipping zero chunks, then compare every byte and run final `e2fsck -fn` (exit0 required).
7. Require successful VMM exit and the verification marker. Sync the final file, write/sync the promotion journal, preserve current as previous, install final, sync capacity settings, then remove/sync the journal. Until journal commit, startup recovery restores the previous disk and clears stale auto-growth.
8. The previous disk remains after success. Restoring clears scheduled growth before one `renameat2(RENAME_EXCHANGE)`; unsupported exchange fails, with no unsafe rename fallback. Do not delete an ambiguous sole remaining candidate.

All writes are guest blocks or this app's data files. There is no host mount, kernel/module operation, global network/memory change, reboot, SELinux change or partition write. Temporary root loss/Android process termination may interrupt a job; next preparation recovers the owned journal before another writer starts. Recovery protects the previous snapshot, not unsaved work held only in memory.

## Source map and diagnostics

- `native/storage_admin.h`: fixed-path CLI, ownership lock, temporary-space check, tracked VM, progress/cancellation and usage.
- `native/storage_state.h`: promotion and recovery with retained previous disk.
- `native/guest_storage.h`: guest-only offline fsck/resize/zerofree/copy and running filesystem statistics.
- `tools/fetch_storage_runtime.py`, `third_party/zerofree/`: pinned Debian ARM64 helper, complete source and licence provenance.
- `tests/test_storage_state.py`, `tests/fixtures/storage_state.c`: promotion interruption and hostile-path checks.
- `tools/build_storage_probe.py`, `tools/guest_storage_probe.c`: optional readonly file-content manifest comparison. Never attach a writer concurrently.

Private per-profile `storage-console.log`, `storage-vmm.log` and `storage-create.log` show stages/failures. They are not published automatically. IPC cancellation accepts only root helpers on its separate abstract socket; manager usage request `S` accepts root/module only, never starts a VM and returns no console/file contents. Clients verify the server UID0.

The UI keeps a job in the foreground; this version does not provide a persistent Android background worker for long maintenance. Keep Bridge open. Do not uninstall/clear its data while a VM/disk job is active.

## Tool references

[resize2fs](https://www.man7.org/linux/man-pages/man8/resize2fs.8.html) supports offline shrink; its estimated minimum is not a universal guarantee. [Debian zerofree](https://manpages.debian.org/bookworm/zerofree/zerofree.8.en.html) requires an unmounted or readonly filesystem. This implementation uses unmounted copies for both operations.
