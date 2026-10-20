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

## Retained-log chronology correction, later 2026-10-19 review

Offline review found earlier forward 4096/3072 MiB refault+collapse captures with
237 and 231 Bad page cache reports, before the reverse trial's 242, all in the
same unchanged boot (710 retained distinct records total). Do not call reverse
order the first occurrence or treat all earlier trials as clean. Initial map-10
failures predated these warnings; multiple failure stages must stay separate.
This source/log review authorizes no new VM, allocation or host setting change.

## Preallocated-RAM static review, later 2026-10-19

The pinned installed crosvm's MiniDebugInfo identifies its actual RAM functions.
Its file-backed region path sends GH_VM_ANDROID_MAP_CMA_MEM; the active-slot
kernel's CMA parcel path checks dedicated gunyah_cma_fops. Do not pass an ordinary
memfd through that option as a presumed normal-RAM import or probe it with an
invalid FD. The ordinary shared constructor creates a memfd by raw syscall,
ftruncates and applies seals before mapping. A potential adoption path belongs
there in a separately reviewed private userspace VMM, preserving all region
roles/offsets/seals and protection flags. It is not implemented or boot-tested;
no new VM/allocation or host-setting permission is implied by this design.

## User-requested normal reboot and bounded FD handoff, later 2026-10-19

The user explicitly requested one normal Android reboot, which was sent only
to the selected Galaxy. This one-time instruction did not authorize future
reboots. The user restored temporary root. Read-only admission in the new boot
showed taint 4608, without BAD_PAGE/OOPS bits; SELinux stayed Enforcing and all
three small-shmem policies remained `never`.

A fixed 16 MiB ordinary-memfd handoff probe then ran once with the normal health
guard, owner lock, memory floor and owned-parent watchdog. Exec'd consumer
identity/seals/offsets/full-content checks passed, and all 4096 observed PFNs
matched before/after. THP coverage was zero. This is not crosvm import, a VM boot,
Gunyah donation or a large-memory/GPU result. No host setting or guest disk was
changed. Preserve the earlier spent GPU-exception marker as historical evidence;
do not weaken production guards or restore mapped hole punching.

## Additional bounded shmem exception: explicit approval, later 2026-10-19

The user explicitly approved adding only the following three temporary host-wide
`never` to `advise` changes, in addition to the already approved 16/32/64 KiB:

- `/sys/kernel/mm/transparent_hugepage/hugepages-128kB/shmem_enabled`
- `/sys/kernel/mm/transparent_hugepage/hugepages-256kB/shmem_enabled`
- `/sys/kernel/mm/transparent_hugepage/hugepages-512kB/shmem_enabled`

Use the distinct medium-policy developer build and its explicit application
token. First prove useful larger folios and full data retention in an owned
16 MiB handoff; verify both independent restorers and all six original `never`
values. Only after that gate may diskless protected diagnostic VMs proceed.
Each policy window remains at most 40 seconds with an independent guardian.
Keep the 6 GiB combined budget, 1536 MiB MemAvailable floor, owner lock, host
health/new-kernel-report guards and all existing host prohibitions. This does
not authorize 1024/2048 KiB settings, global compaction/cache-drop/defrag writes,
Android restart, modules, kernel/protection/partition changes or other VM access.
The production APK must not silently adopt these settings.

## Verified separate VMM result after six-key approval, later 2026-10-19

An app-private AOSP Android16 userspace VMM now explicitly adopts a prepared,
size-sealed ordinary memfd through GuestMemory::create_shm. It is developer-only,
not a replacement for the stock installed VMM. Preserve identity/seal/offset
checks, original protected LEND and 32 MiB shared SWIOTLB roles, and no fallback.
The final baseline SHA256 is
`962b69814df0a60995f5127a2c1ff94730592eb07bf739d1dfa9d6ca0416dc09`.

With the six approved 16–512 KiB shmem advice sizes, atomic owned pressure/release
and normal-zone-first preparation, a 4480 MiB diskless protected Linux VM checked
all 4,294,967,296 bytes of its 4096 MiB workload twice, with zero errors, positive
completion, normal exits and both restorers. All six policies returned to never,
taint stayed 4608 and SELinux Enforcing. Three-key trials had established 3 GiB;
write-only progress near 4 GiB was not readback. Record all failed comparisons.
The separate split-LEND and pin/collapse variants did not fix the measured 4 GiB
workload; the successes use the baseline without them. Do not resurrect the
retired GuestMemory subdivision or bypass its DTB/payload-region assertion.

This result does not validate all 4480 MiB, Android 17, GPU, attached disks,
long-running stability or an automatic production app profile. Do not silently
add the six-key global policy to the released APK. Keep new Android/GPU and
persistent-disk comparisons separate and preserve their original data. Source,
limits and aggregate results are in docs/preallocated-vmm-validation.md.

## Explicit Android/GPU 4 GiB trial request, 2026-10-19

The user now explicitly requests Android and GPU at 4 GiB. The existing six-key
temporary shmem exception may be used with this separate developer Android
profile, retaining the same 40-second guardian, preparation-ready restoration,
6 GiB combined cap and normal healthy-boot guard. The earlier diskless-only
validation is not an Android result; do not reuse its success claim. Use a
verified offline copy of Android working/misc/FRP disks in a new profile, keep
all original disks and host boundaries intact, and pin the separate source-built
VMM, guest kernel, ramdisk and renderer. No new affected-boot exception or host
restart is authorized. This does not silently enable the path in the APK.

## Android 4 GiB GPU developer progress, 2026-10-20

The five separate Android/GLES trials and negative results are recorded in
docs/android4g-gpu-investigation.md. The aligned six-key comparison reached
guest adbd/root setup and initialized host Adreno 840 gfxstream, without a
recorded PAGE_FAULT attempt error during its bounded observation. Guest
SurfaceFlinger instead aborted on EGLConfig selection; BOOT=1, SystemUI and
a verified GPU frame were not reached. Do not claim a working Android GPU UI,
integrate an automatic large GPU profile or discard the prior failed cases.
Preserve the fixed archived trial profiles and original disks. Follow-up GPU
protocol logging remains developer-only, bounded and payload/address-free.
The same healthy-boot, six-key/40-second, 6 GiB and no-host-restart limits apply.

## Mac build storage update, user instruction 2026-10-20

The user explicitly asked to continue the guest-kernel build rather than stop
at the earlier 20/21 GiB convenience threshold, and clean up after compilation.
Keep monitoring actual free space and write failures, but do not stop a healthy
build merely for crossing that earlier threshold. After the build, retain the
Image, final config, System.map/provenance/logs, exact compressed source archive
and public reproduction recipe; remove only this build's verified owned source
extraction/intermediates when no longer needed. Never prune unrelated Docker
images, caches, projects or device data. Host Android constraints are unchanged.

## Separate Android guest kernel and GPU follow-up, later 2026-10-20

A separate Android common 6.12.74 guest Image with the upstream guest-bound
DMA-completion sync backport built successfully. Its virtual devices are built
in, modules/nested VM-manager interfaces/debug-kinfo are disabled inside the
guest only. The Image/config/System.map/archive/provenance remain private;
owned Mac source/object intermediates and the build volume were cleaned after
compilation. No original guest Image or host kernel was replaced.

Trials 8/9 reached BOOT=1 and guest Adreno EGL with this Image. Trial 8 later
failed Gunyah PAGE_FAULT -12; trial 9 had no recorded page-fault error during
160 seconds but SystemUI/Launcher crashed on buffer allocation -129. No
nonce-bound GLES test PNG or usable Android GPU desktop is verified.

The pinned VMM derives Vulkan enablement from its capset mask, overriding the
explicit false argument. Trial 10's capset comparison aborted in global loader
lookups. An optional pinned process-local loader bridge, restricted to the
owned source-built VMM, repaired those three lookups in trial 11; the renderer
then aborted on a zero-requirement 64x64 test ColorBuffer allocation. No memory
or Gunyah ioctl interposition, fake requirements or host library change is
authorized by that bridge. Keep full-capset/loader options developer-only,
explicit and independently pinned; do not add them silently to the APK.

All trial 8–11 postflights retained the host boot/core processes/trusted VM,
original Android disk hashes, taint 4608, Enforcing and all six never values.
These observations do not remove the earlier risk/health/RAM/policy guards.
Details and failed cases are in docs/android4g-gpu-investigation.md.

Trial 12 uses the renderer's explicit `Vulkan:disabled` feature override with
format capsets, without the loader bridge. It reached BOOT=1/Adreno EGL and
lasted the 160-second observation without recorded Gunyah page-fault errors,
but window allocation -129 and SystemUI crashes remained. No frame is verified.
All host/disk/policy postflight checks passed. Android Binder also uses -129
for EX_TRANSACTION_FAILED, and Gralloc5 propagates that exception code; do not
call this a cryptographic-key failure or RAM shortage from strerror alone.

## Explicit GPU app integration, user request after real rendering, 2026-10-20

Trial 16 established nonce-bound two-phase Adreno GLES readback and a separate
visible Android home PNG. Trial 17 still failed Gunyah admission before boot;
startup reliability and the combined nonce-pattern PNG check remain unresolved.
The user explicitly requested app integration. Preview.8 therefore adds only a
separately selected 4096 MiB experimental profile, with per-launch visible
six-key acknowledgement, original 40-second guardian/preparation-ready restore,
6 GiB budget, healthy-host/platform/artifact guards and no automatic retry.
Keep legacy profiles, original OS disks and Debian intact. The 128 MiB I/O pool
is inside 4096 MiB; normal protected LEND and host boundaries are unchanged.
The app's initial 4 GiB launch showed a real guest lock screen. Input repair
forwards bounded swipes instead of converting every gesture into a tap; do not
disable falsing/security policy or send guest unlock commands to host Android.


## GPU Surface default requested after integrated validation, 2026-10-20

The user explicitly requested GPU direct display as the initial selection, with
slow PNG capture disabled by default. The launch dialog now initially selects
4096 MiB and the GPU Surface switch, retaining the explicit launch action and
full per-launch six-key acknowledgment. The GPU branch issues no raw/PNG capture
requests and must not silently fall back to capture. The old viewer remains an
explicit manual choice. This does not authorize automatic VM start, a broader
host policy change or removal of host-health/artifact/RAM/guardian guards.

The integrated versionCode 18 session reached BOOT=1, two-contact virtio input
and actual validated guest Internet. Texture update counts are not proof of
60 presented fps or zero-copy. Details, negative trials and limits remain in
docs/android-display-transport.md. Android guest audio/camera are not yet
implemented; Debian's audio relay is not an Android HAL.


## Vulkan guest trial: host restart observed, 2026-10-20

The new private gfxstream build fixed the global-loader lookups, unbound AHB
requirements query, dedicated export and incompatible AHB/DMA_BUF export mask.
A no-VM host probe initialized Vulkan on Adreno and completed teardown. This
does not validate guest Vulkan. The first isolated Android trial reached ADB
but stopped when its authenticated viewer client was missing. The next trial
reached root guest ADB with the ranchu Vulkan selector. Its guest Vulkan probe
reported Bus error before the device/readback markers, host ADB disappeared,
and the user confirmed Android had restarted. No host reboot command was sent.
No Vulkan readback or clean second-trial postflight is established.

Do not start this Vulkan VM or new GPU/allocation trials pending retained-crash
review and explicit follow-up. The diagnostic manager, policy helper and RAM
proxy are now execution-disabled at entry, including --check, before any
allocation/disk/policy work. Do not remove that
guard merely because a new host boot lacks taint. Building and read-only
collection of pstore/boot reason/logs are allowed; do not clear crash records,
reboot, change protections or restore temporary root by a different method.
The released preview.9 GLES path has not been changed to enable Vulkan. Keep
its persistent disks, the separate Vulkan clone and failed-case evidence.

Read-only post-restart collection recovered `/proc/last_kmsg`: the host really
panicked with `synchronous external abort`, in `crash_dump64` through
`process_vm_readv -> process_vm_rw -> copy_page_to_iter -> __arch_copy_to_user`.
This establishes the fatal crash-recording path, not the identity or initial
failure of the process being inspected. No completed tombstone for that moment
was retained. Reading protected/donated or externally mapped memory is a
hypothesis, not a proved mapping attribution. Do not reproduce it by reading
live VMM memory, attaching a debugger or deliberately crashing a VMM. The
normal crosvm mapping constructor already requests MADV_DONTDUMP; do not assume
adding it again prevents debuggerd's explicit process_vm_readv accesses.

After restart, read-only checks found root available, taint 4608, Enforcing,
all six shmem policies at never, and all four original Android source-profile
hashes unchanged. These checks are not a clean postflight of the failed trial
and do not authorize more VM/GPU/allocation trials.

## One normal GLES retry for requested game installation, 2026-10-20

The user explicitly approved one normal 4096 MiB GLES VM retry to diagnose
network access and install the Japanese Garupa APK after the user-started normal
GLES VM exited with Gunyah PAGE_FAULT -12 during APK transfer. This does not
authorize Vulkan, another automatic retry, larger RAM or broader host changes.
Retain the production health/lock/RAM/six-key/40-second-guardian checks. Stop on
new errors; record consumption when dispatching this one launch. Existing
Android/host protection and no-reboot constraints remain unchanged.

The first approved retry was dispatched and ended before VMM execution with
`Surface unavailable`; it is consumed. The user then explicitly approved one
additional normal GLES startup attempt with the viewer visible, for the same
network/game installation task. No further automatic launch is authorized.

The additional attempt is now consumed. It reached BOOT=1/root guest ADB but
APK transfer again ended with Gunyah PAGE_FAULT -12 at 08:08:26 JST. Garupa was
not installed and the HTTPS/Aurora checks did not complete. Read-only postflight
retained the same host boot ID, taint 4608, Enforcing and all six never values.
Do not retry another VM/startup/allocation trial under either spent approval.


## Android 17 branch isolation, user request 2026-10-20

- `android16-frozen` and `android16-final` are fixed at d38015f; do not move them
  or change the Android 16 preview.9 release. Its device is no longer available.
- `codex/android17-host` targets Android 17 only (min/target SDK 37). Do not add
  Android 16 runtime branches or backport the new storage UI into the frozen line.
- No Galaxy is available now. ROM inspection and host builds are not device tests.
- The inspected SM-S948Q S948QOPU1BZI9 ROM has kernel 6.12.69, Terminal versionCode
  37 and a different stock VMM. Its virtualization APEX has no gfxstream backend.
  Do not substitute old hashes or declare Android/GPU guests compatible. This
  branch omits their old bundled runtime; retain guest data unchanged.
- Keep inspection-heavy images on Windows/WSL, not the space-constrained Mac.
