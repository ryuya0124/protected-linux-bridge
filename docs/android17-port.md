# Android 17 host port

Status: Android 17 host/Terminal admission, bounded CPU VMs and no-VM Adreno
owned-Surface presentation have passed device checks. **The new Android guest
path has not completed boot; the production Android gate remains disabled.**
The [current runtime/fragmentation validation record](android17-runtime-validation.md)
separates successes from failed Android trials. Earlier sections retain their
original test scope; later explicit GUI/VM authorization superseded the prior
non-GUI-only pause. Host Android and guest Android versions remain distinct.

## Frozen Android 16 line

`android16-frozen` and `android16-final` point to `d38015f` and must not move.
The unchanged distributed APK is release `v0.4.0-preview.9`, versionCode 19,
77,489,151 bytes, SHA256
`7853c4d842ff3b097d70bdf1c25381be54b0a77460886fb46ce0ab5a2984fd2e`.
The freeze includes the subsequent panic/stability records. It is not a claim
of deterministic byte-for-byte rebuilds. No Android 16 hardware remains for
new tests; do not backport this branch's changes.

`codex/android17-host` targets Android 17 only, min/target SDK 37. Version
`0.5.0-alpha.1` / code 20 began the separate experimental port;
`0.5.0-alpha.2` / code 21 adds app-only Debian setup and selected backup/restore. The same package and
signer retain private guest data on an ordinary update. Downgrading or uninstalling
is not a data migration procedure; preserve private disks before such operations.

## ROM evidence

Source: official Ultra ROM archive
`SM-S948Q_1_20260914084841_63d8br85sd_fac.zip`, AP
`AP_S948QOPU1BZI9_S948QOPU1BZI9_MQB114766720_REV00_user_low_ship_MULTI_CERT_meta_OS17.tar.md5`.
The archive was read on Windows/WSL; the full ROM was not copied to the Mac.
This identifies the inspected ROM, not the firmware actually running on the
phone at the time of the offline inspection. Later device checks confirmed the
matching ROM/VMM/kernel/APK identities. Hashes, sizes and results are recorded in
[`compatibility/android17-s948q-bzi9.json`](../compatibility/android17-s948q-bzi9.json).

| Component | Android 16 record | Inspected Android 17 ROM |
| --- | --- | --- |
| Model / build | SM-S948Q / S948QOPS1AZHL | SM-S948Q / S948QOPU1BZI9 |
| SDK / One UI | 36 | 37 / `90000` |
| Host kernel | 6.12.30-android16-5 | 6.12.69-android16-6 |
| Terminal versionCode | 36 | 37 (`versionName=17`, min/target 37) |
| Stock crosvm SHA256 | `9b4d56db70be1d3d646a08da0cb3e6eb95637457e5f15cb3b1c627f348e3e847` | `a0cfa5b957d119202b85a069928b3b56bd080329efed42eb83b182c9513885e9` |
| Separate APEX gfxstream library | present | absent from `/lib64` |

The new host kernel release is
`6.12.69-android16-6-pee899be-abogkiS948QOPU1BZI9-4k`.
The `android16` part is in the actual kernel release; it does not mean the ROM
is Android 16. The kernel **changed**; assumptions from 6.12.30 must be rechecked.
Embedded configuration confirms Gunyah/hypercalls/IRQFD/IOEVENTFD and 4 KiB pages,
shmem, THP with MADVISE default and CMA. HUGETLBFS is disabled. These build options
do not establish runtime shmem policy, folio allocation success, recovered
Gunyah map resources or a fix for prior ENOMEM/BAD_PAGE/panic failures.

The APEX is EROFS, not ext4. `debugfs` could not read it; extraction was repeated
with read-only `dump.erofs`. Its `--cat` can return status 0 for a missing path,
so the file length and directory listing were checked as well. Terminal is at
`/priv-app/VmTerminalApp@CP2A.260605.016/VmTerminalApp.apk`.

## Terminal contract

The extracted APK was inspected with `aapt2` and JADX. These actual members remain:

- `MainActivity.startVm()`, `connectToTerminalService(TerminalTabFragment)`, `onDestroy()`
- Main fields `tabAddButton`, `displayMenu`
- `TerminalTabFragment.onViewCreated(View, Bundle)`, `onDestroy()`
- Fragment fields `terminalView`, `bootProgressView`, `ttydStatus`, `ttydTimeoutRunnable`
- `TerminalView extends WebView`
- `InstalledImage` still resolves `files/linux/root_part` and `root_part_backup`

This is static verification, not proof of hook loading, caller deoptimization,
fragment lifecycle, accessibility behavior or a VM boot. The module verifies the
complete reflective contract before installing hooks and rolls back on failure.
Admission now requires the inspected Android 17 model/build/kernel, Terminal 37
and the extracted APK's SHA256. Native Debian admission independently checks
model/build/SDK/kernel, SELinux Enforcing, host health and both stock VMM/kernel
hashes. A matching identity is an admission condition, not a stability claim.

## GPU difference and retained data

The old app loads `/apex/com.android.virt/lib64/libgfxstream_backend.so` and uses
its measured ABI from a private source-built VMM. That library is absent here.
Follow-up saved-image analysis now confirms actual built-in gfxstream and
Android AHardwareBuffer display function bodies, beyond strings. The stock
executable is PIE and has no dynamic renderer/Bridge exports; MiniDebugInfo is
not a callable loader API. See the [GPU display investigation](android17-gpu-display.md)
for the AHB import route, per-session Binder design and global-service trap.
Neither static implementation presence nor source comparison establishes
working GPU presentation. Do not describe the ROM as lacking GPU support
solely because the old separate library is absent.

The new product build omits old Android/GPU assets and removes only their stale
**generated build outputs**, never phone data. Android guest startup fails before
asset installation or root/policy/disk work, and the settings card explains why.
Debian's separate virgl runtime is retained but unverified on this host.
No Vulkan path is enabled. The prior host panic and its execution guard remain.

Next qualification work needs the actual device: verify active APEX identities,
Gunyah UAPI and protection roles, review the new renderer entry points/protocol,
then perform separately authorized bounded diskless and copied-disk tests.
Do not replace checksums merely to make the old VMM launch. Never load a host
module, alter SELinux, patch protections or modify host partitions.

## Storage screen

Settings → VM asset storage opens a non-exported Material 3 activity. The bounded
native inventory reads only eight fixed private categories, distinguishes
`st_blocks * 512` from file length, reports device filesystem free/total bytes
and marks partial results. It does not interpret QCOW2 file length as virtual
capacity or guest free space. Shared blocks/hardlinks may be counted repeatedly.
System APEX files, app APK, arbitrary external storage and unknown private
categories are outside these totals.

It never follows symlinks, crosses filesystems, recursively deletes data or
starts/stops a VM. Depth and entry counts are bounded. Busy lock observation is
advisory; inventory is not an atomic filesystem snapshot. Root denial/timeouts
show an error, not zero usage. VM disks, backups and kernels have no deletion
button because their full dependency graph has not yet been qualified. Existing
offline disk resize/compaction/recovery controls stay on the settings screen.

## Reproduction and verification

Keep ROM images on the roomy Windows/WSL disk. Stream the AP `super.img.lz4`,
expand Android sparse chunks and validate LP metadata/checksums; extract only
`system_a`. Use `dump.erofs --cat --path=/system/apex/com.android.virt.apex system_a.img`,
check nonzero output, then extract the APEX ZIP's `apex_payload.img`.
`dump.erofs --ls --path=/lib64 apex_payload.img` establishes the separate library
absence. Read the three paths in the compatibility JSON with `--cat`, require
nonempty files, compare SHA256, then run `aapt2 dump badging` on the APK.
Review actual classes with JADX rather than inferring member names from AOSP.
No mounting or host configuration changes are needed.

Mac builds do not need a full ROM or AOSP checkout. Signed release build, unit,
native and Lint results are recorded with the branch commit; they do not replace
device validation. The Android 16 APK is preserved privately before its shared
Gradle output path is reused for the Android 17 build.


## Offline build result, 2026-10-20

- Signed non-debuggable release APK: versionCode 20 / `0.5.0-alpha.1`,
  min/target SDK 37; 20,042,199 bytes.
- SHA256: `b6feb8db977a0654ff0826c0e8081aab04f92650f4c6876eb9e0e26739937356`.
- Signature verification passed; the signer matches the frozen Android 16 APK.
- 204 Python/native tests and 42 JVM tests passed. Release Lint: zero errors
  (existing warnings remain). The new toolbar uses an app-owned back icon.
- APK inspection confirms the read-only inventory and Debian runtimes are present
  and old Android/GPU runtime assets are absent. The preserved Android 16 APK's
  SHA256 remains unchanged.
- None of these are device boot/rendering/storage-UI interaction results.
- After hashes and selected artifacts were retained, only this inspection's
  `super.img.lz4`, `super.raw` and `system_a.img` intermediates were removed from
  its private WSL work directory (44,749,665,097 logical bytes). Original ROM,
  original analysis files, selected APEX/VMM/kernel/APK and other chats' data
  were retained. Sparse logical bytes are not a physical free-space claim.


## Owned display / Terminal follow-up, alpha.2

[Implementation, device checks and remaining limits](android17-owned-display.md).
The dedicated shell reflection probe resolved the complete Terminal contract
on the inspected Galaxy, including inherited AndroidX view teardown. The owned
ARM64 backend loaded and its nine display ABI entries, Binder-pool entry and
AHB import entry resolved. Neither probe starts a VM/GPU/display or installs
a hook. Host boot/core process identities, taint 4608, Enforcing and all six
shmem `never` values stayed unchanged. Temporary probe files were removed.

The source changes repair Terminal view recreation and provide dormant private
display transport components. New VMM/renderer/launcher integration and actual
Vector interception are still pending. See
[`compatibility/android17-owned-display-build.json`](../compatibility/android17-owned-display-build.json)
for the signed release artifact and build/test counts.

## CPU/readonly Debian progress and app-only setup

Subsequent explicitly authorized non-GUI tests completed two 512 MiB / one-vCPU
protected VMs with the pinned stock VMM/Microdroid guest kernel. One was diskless;
the other mounted only the Terminal root_part read-only/noload and returned
`arm64` from guest dpkg. Both checked a nonce-bound 16 MiB payload. The source
disk's complete hash and the host/policy boundary stayed unchanged. Watchdog
and shutdown-channel warnings remain recorded; these are not full Debian GUI,
writeable-session, GPU or Android guest validation. The guest kernel reports
6.6.118 while the host is 6.12.69.

The app now includes compressed selected-root backup/restore, retained-copy
recovery, shared/exclusive maintenance serialization, fixed-image Debian setup
and an in-app multi-tab terminal. [Backup behavior](backup-and-restore.md) and
[Debian setup](debian-setup.md) describe user steps and limits. The storage
inventory additionally counts `debian-images`, `backup-recovery` and `backup-jobs`.

The exact Google ARM64 Debian archive was verified end to end with the same Java
installation codec on the development host. Image extraction/checksum success
is not an Android installation test. New setup/backup UI and the complete
writeable VM session await device access. The user manually soft-rebooted and
requested a pause. No device operation was performed during that pause.

## Non-GUI access resumed and UI reorganization

The user subsequently unlocked the Galaxy and permitted non-GUI tests again.
Read-only admission confirmed the same Android 17 host contract, root, locked
bootloader/green AVB and Enforcing. No Bridge VM was running. An independent
developer-only test used the production backup codec and native maintenance
locks on 2 MiB of synthetic files in a fresh app-private test directory. Selected
restore, retained originals and checksum/rejection checks completed. All test
files were removed; host boot/core/trusted-VM identities, taint 4608 and all six
shmem policies remained unchanged. No VM, GPU or physical GUI was started.

The test first exposed SIGKILL during NIO FileStore discovery. Direct per-path
free-space queries fixed the observed failure; the signal sender is unproven.
The same fix is included in Debian image installation. This small codec test
is not a full real-disk restore or Debian installation test.

The [Material 3 navigation](app-navigation.md) now separates Overview, VMs,
Devices and Settings, with clearly scoped Debian details and a separate disabled
Android-guest entry. UI tests ran in an owned Android 17 emulator, including
Japanese/English, dark mode, larger text and rotation. These are not Galaxy
GUI or guest rendering results.
