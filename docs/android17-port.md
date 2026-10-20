# Android 17 host port

Status: offline inspection and implementation, **no Android 17 device validation**.
The owner upgraded the only Android 16 test device; a device is unavailable now.
Host Android version and Android guest version are separate concepts throughout
this record. No VM, host configuration change, reboot or flash was performed.

## Frozen Android 16 line

`android16-frozen` and `android16-final` point to `d38015f` and must not move.
The unchanged distributed APK is release `v0.4.0-preview.9`, versionCode 19,
77,489,151 bytes, SHA256
`7853c4d842ff3b097d70bdf1c25381be54b0a77460886fb46ce0ab5a2984fd2e`.
The freeze includes the subsequent panic/stability records. It is not a claim
of deterministic byte-for-byte rebuilds. No Android 16 hardware remains for
new tests; do not backport this branch's changes.

`codex/android17-host` targets Android 17 only, min/target SDK 37. Version
`0.5.0-alpha.1` / code 20 is a separate, experimental port. The same package and
signer retain private guest data on an ordinary update. Downgrading or uninstalling
is not a data migration procedure; preserve private disks before such operations.

## ROM evidence

Source: official Ultra ROM archive
`SM-S948Q_1_20260914084841_63d8br85sd_fac.zip`, AP
`AP_S948QOPU1BZI9_S948QOPU1BZI9_MQB114766720_REV00_user_low_ship_MULTI_CERT_meta_OS17.tar.md5`.
The archive was read on Windows/WSL; the full ROM was not copied to the Mac.
This identifies the inspected ROM, not the firmware actually running on the
currently unavailable phone. Hashes, sizes and results are recorded in
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
The new crosvm contains `gfxstream::host::FrameBuffer`, `ColorBuffer`,
`VirtioGpuFrontend` and renderer initialization strings; its ELF NEEDED list
contains no gfxstream shared library. This suggests a built-in renderer, but
symbols/strings alone do not establish a callable ABI or working GPU path.
Do not describe the ROM as lacking GPU support solely because the old file is absent.

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
