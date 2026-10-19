# Android 17 at 4 GiB: RAM and GPU investigation

Updated 2026-10-20 JST. Work is ongoing. **Trial 8 reached Android boot-complete and SystemUI with a separate guest
kernel. A hardware-rendered guest test frame is still unverified.** The released
APK has not been changed by this developer experiment.

## What is established

- A separate app-private AOSP crosvm adopts one prepared, size-sealed ordinary
  4 GiB memfd, with identity, size, access and seal checks. No RAM copy or
  installed-VMM replacement is involved.
- Android 17 Cuttlefish build `16373615`, kernel `6.12.74-android16-6`, starts
  its kernel and userspace. Trial 5 reached its own adbd; the identity-checked
  observer saw `MemTotal: 3976832 kB`, SurfaceFlinger and guest-root bootstrap.
  `sys.boot_completed` remained unset and SystemUI was absent.
- Trial 5's host renderer initialized successfully as **Android Emulator
  OpenGL ES Translator (Adreno (TM) 840)**. This is host initialization,
  not proof that the guest successfully rendered a frame.
- That trial continued until the 160-second owned-manager observation limit,
  with no recorded Gunyah PAGE_FAULT attempt error. It encountered a different
  blocker: guest SurfaceFlinger aborted with `no suitable EGLConfig found`.
  Its log also reported an unrecognized GLES version string, `pipe:opengles`.
  This points the next comparison toward EGL/guest-host transport; it does
  not yet identify the exact defect.

The earlier [diskless Linux result](preallocated-vmm-validation.md) checked a
4 GiB workload twice. It remains a separate result: neither that RAM test nor
adbd access establishes a booted Android desktop or GPU rendering.

## Controlled comparisons

Trials 1–7 used 4096 MiB, two CPUs, the same pinned guest kernel/ramdisk, a fresh
offline copy of the original three writable Android disks, a 480×800 GLES
configuration and a 256 MiB GPU BAR address window. The BAR size is an address
aperture, not a claim of 256 MiB physical GPU allocation. Vulkan was disabled.

| Trial | Preparation | Advised shmem sizes (KiB) | Last observed THP coverage | Outcome |
|---|---|---|---:|---|
| 1 | Normal guest zone first, ordinary virtual alignment | 16–512, six sizes | 92.5289% | Gunyah PAGE_FAULT attempt `-12`, exit 33 before boot completion |
| 2 | Ascending file offsets, ordinary virtual alignment | 16–512, six sizes | 98.4592% | Same error, exit 33 |
| 3 | Ascending, ordinary virtual alignment | 128/256/512 only | 0% | adbd sample, then same Gunyah error, exit 33 |
| 4 | Ascending, explicit 2 MiB virtual alignment | 128/256/512 only | 28.0487% | Same Gunyah error, exit 33 |
| 5 | Ascending, explicit 2 MiB virtual alignment | 16–512, six sizes | 99.9996% | adbd/root bootstrap; host Adreno initialized; guest EGLConfig failure; observation timeout 124 |

Coverage is a **pre-donation snapshot**, not a pin or a stability guarantee.
The last snapshot per file-offset batch is counted once when a PTE refresh
requires a second observation. These observations show that preparation order,
alignment and enabled sizes affect the supplied backing. They do not isolate
physical allocation geometry from changing host memory pressure, prove a causal
fix, or identify the exact exhausted Gunyah/EL2 resource.

The narrower 128/256/512 comparison first ran a 16 MiB handoff gate. Without
owned pressure/release it retained data but supplied no THP. After the bounded
preparation it supplied 4096 THP pages in 32 heads (512 KiB folios), retained all
data and the same observed PFNs across exec, and restored all settings. Large
trials still differed, which is why the small gate alone is insufficient.

[Sanitized measurements](data/android4g-gpu-2026-10-20.json) retain the negative
results. Raw console/crash traces, identifiers, writable-disk hashes, images,
binaries and signing material stay in ignored private artifacts.

## Host and disk protection

The six explicitly approved `hugepages-{16,32,64,128,256,512}kB/shmem_enabled`
nodes were changed only within the bounded diagnostic. The narrowed comparison
left 16/32/64 at `never`. The main controller restored on preparation readiness;
an independent guardian retained the maximum 40-second policy window. Both
restorers acknowledged success in every listed trial and all six values were
`never` afterwards. No 1024/2048 KiB node was changed.

The combined explicit RAM limit remains 6 GiB. Owned pressure/release is
sequential with VM preparation, never simultaneous with the VM. The 1536 MiB
MemAvailable floor, healthy-boot checks and common owner lock remain. The latest
manager also samples its owned child's RSS, leaving 256 MiB inside the cap for
supervision; RSS does not measure every GPU allocation. New kernel-report read
loss or BUG/Oops reports stop the owned trial. The original protected LEND and
32 MiB shared SWIOTLB roles remain in place.

Trials 1–12 ended in the same host boot, SELinux Enforcing, taint 4608 with
no BAD_PAGE/OOPS bits, and unchanged main Android processes/unrelated trusted
VM. Those checks are bounded observations, not proof of zero host risk. No
Android restart, host module/kernel/partition/protection change, mapped hole
punching or retired refault fallback was used.

The original Android working/misc/FRP disks were hash-verified before and after
each fresh copy. Failed profiles are retained under fixed archive names.
The copied qcow2 overlay refers to the existing read-only base image. Existing
Debian and selectable production profiles are not replaced.

## Reproducible developer source

- `tools/build_preallocated_vmm.py --android-gpu` builds the pinned AOSP VMM
  with actual gfxstream, qcow2 and Android sparse-image support. Its Android
  headless-display patch uses the existing host display stub, independently
  of the actual GPU renderer. One unsupported X-display fallback message is
  not a renderer failure. No gfxstream renderer stub is enabled.
- `tools/build_android4g_probe.py` pins the VMM build manifest, guest kernel,
  GLES initramfs, fixed signed guest test APK and launch proxy. It builds but
  never installs or starts a VM. `--minimum-folio-kib 128` builds the narrower
  comparison; the default is the six-size comparison.
- `tools/android4g_profile_copy.c` holds the native common lock while the fixed
  root-owned `tools/prepare_android4g_profile.sh` copies/verifies disks. Android
  toybox flock accepts an inherited FD; it does not support GNU's filename plus
  command syntax. Use the native supervisor, not shell-redirection FD tricks.
- `tools/preallocated_vm_probe.c` has a separate fixed Android proxy build.
  It keeps the same PID through exec, preserving the console peer credential
  check. The Android batch mappings reserve fresh aligned address space; this
  neither invokes collapse nor remaps an existing live backing allocation.
- `PLB_ANDROID_ADOPTED_PROBE` is rejected outside the isolated healthy-boot
  GLES diagnostic or when mixed with legacy memory/Vulkan/one-shot-warning
  profiles. It is not an APK launch option.
- The observer uploads only the fixed signed guest GLES test after successful
  Android boot. Positive verification requires renderer identity, nonce-bound
  two-phase GLES readback and the corresponding PNG via
  `tools/verify_guest_gpu_frame.py`. None was obtained in trials 1–5.
- `tools/android4g_guest_diagnostics.c` reads only fixed diagnostics after
  verifying the guest's device, hardware, build and UID. It cannot accept an
  arbitrary command, upload, root request, settings change or forwarding port.

The next private VMM build adds bounded GPU context/transfer metadata logging
through `tools/crosvm_gpu_diagnostics.patch`, without logging payload bytes or
addresses or changing transfer return values. Compare resource/context routing,
EGL configuration and guest-host transport before considering a new renderer.
Keep all failed trials and do not expose an automatic 4 GiB GPU app option until
Android boot and actual frame verification pass.

## Follow-up: guest-bound transfer investigation

Trial 6 retained the aligned six-size, 4096 MiB pipe configuration and added
bounded metadata tracing. It again reached guest adbd/bootstrap without a
recorded Gunyah PAGE_FAULT attempt error, but timed out at 160 seconds without
BOOT=1. Real gfxstream context creation and transfers in both directions
returned zero. This establishes routing to the renderer; a zero transfer return
is not proof that guest userspace sees the returned bytes.

Offline inspection extracted the actual guest `lib/modules/virtio-gpu.ko`
from the pinned ramdisk. Its vermagic matches the guest's 6.12.74 kernel. Its
undefined-symbol table includes `__dma_sync_sg_for_device` and does not include
the corresponding CPU-sync function. The guest-bound transfer function's
disassembly and the Android common 6.12 driver source lack a completion CPU-sync
operation. Together with the stale-looking `pipe:opengles` reply, this makes
SWIOTLB bounce-buffer synchronization a concrete **candidate**, not yet a
confirmed device-specific cause. At that stage no guest kernel/module had been replaced; trial 8 below uses a separate guest Image.

Upstream's guest-bound synchronization fix is
[commit 598c1c3e895590f845e04455d5580ea28ffde666](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=598c1c3e895590f845e04455d5580ea28ffde666).
The fix must account for asynchronous completion and preserve untouched buffer
contents; inserting an immediate CPU sync into the queue submission function
would be insufficient. Keep this separate from the Gunyah RAM-geometry failures.

Trial 7 selected `--transport asg` in the developer builder, retaining
the same memory, kernel, renderer and GLES settings. Cuttlefish supports
[ASG and pipe transports](https://android.googlesource.com/device/google/cuttlefish/+/a2b94a39ae94317df56e55ac95e6c10e8ff1659b/host/libs/vm_manager/crosvm_manager.cpp).
ASG exercises shared ring/blob communication instead of the legacy transfer
path. Its protected-VM shared mapping and actual frame output still need
verification; changing that property alone is not a GPU success.

The GPU VMM build, native helpers and 172 Python/native tests passed before
this follow-up. Built binaries and traces remain private; the release APK
retains its existing settings and guards.

Trial 7 confirmed `ro.boot.hardware.gltransport=virtio-gpu-asg` inside the guest.
However, the renderer trace still showed the legacy pipe handshake, and the
same stale extension/EGL failure remained until the 160-second timeout. There
was no recorded Gunyah PAGE_FAULT attempt error. This is **not** a successful
ASG-ring comparison. The examined current upstream
[HostConnection.cpp](https://android.googlesource.com/platform/hardware/google/gfxstream/+/refs/heads/main/guest/OpenglSystemCommon/HostConnection.cpp)
selects the pipe path for emulation GLES even for either virtio-gpu property;
Vulkan or ANGLE takes a different branch. We must verify the actual selected
transport instead of treating a property value as proof.

The original ramdisk module also matches AOSP's `android17-release` prebuilt,
and that prebuilt lacks the CPU-sync import too. An older `main` prebuilt and
Android 16 QPR2 module were not substituted merely because they were downloadable;
their kernel versions differ. The existing Linux 6.18.57 source archive contains
the upstream guest-bound completion-sync fix, but that Debian kernel lacks
Android's F2FS/encryption configuration and cannot be assumed to boot this
Android image. Investigate a separately built guest kernel/module with the
correct Android filesystem and DMA support; retain the original guest kernel
and all host prohibitions.

## Separate Android guest kernel build

The [guest-only build recipe](../kernels/android-gpu/README.md) now backports
the upstream completion-sync fix to Android common commit
`3ec022196c4e9d5c1434599cdda63f622dd6f586`. It retains Android filesystem,
encryption, Binder and SELinux support, builds required virtual devices in,
and uses a different Image, ramdisk and copied-disk profile. The stock Android
guest kernel, host kernel and Debian environment are retained. The paired
Image/manifest inputs are checked before custom diagnostic helpers are built.

Build preparation exposed a Mac filesystem issue: Linux contains both
`net/netfilter/xt_hl.c` and `xt_HL.c`. Extraction/build on a case-insensitive
Mac volume loses that distinction. The recipe therefore extracts and compiles
on an invocation-owned Linux Docker volume, checks that the two files are
distinct, and exports only completed artifacts. Guest-only unused nested
KVM/VM-manager interfaces are disabled for the built-in-only configuration.
This does not disable the outer host hypervisor or protected-VM checks.

The user approved continuing compilation beyond the earlier convenience disk
threshold and cleaning owned intermediates afterwards. The build preserves
the Image, config, System.map, logs/provenance and exact compressed source.
No unrelated Docker data is pruned. A successful build alone will not be
reported as a successful Android/GPU trial.

## Trial 8: Android boot-complete with the new guest kernel

The separate guest Image built successfully as `6.12.74-plb-android-gpu1`
(SHA256 `1ad9e37e8f2247e3ee566e85beec4ce0a29ef279633163a4c1672234d2cecee5`).
The compiled virtio-GPU object contains the missing CPU-sync reference. The
built-in-only configuration also disables guest bootloader debug-kinfo data,
whose source assumes loadable-module structures; no Android host setting is
changed by that build configuration.

Trial 8 used this Image, its separate initramfs and a new verified disk copy.
The observer recorded **BOOT=1**, a live **SystemUI** and guest MemTotal
`3973592 kB`. Guest SurfaceFlinger successfully initialized EGL and reported
**Google (Qualcomm), Android Emulator OpenGL ES Translator (Adreno (TM) 840),
OpenGL ES 3.1**. The prior stale version string / `no suitable EGLConfig found`
blocker was not present in that diagnostic. This is evidence for the combined
custom-kernel configuration, not an isolated proof that the single backport
alone accounts for the difference.

Before donation, THP coverage was 93.7286% overall: 100% in the first 2 GiB,
87.4573% in the second. After boot completion, Gunyah again returned PAGE_FAULT
attempt `-12` / map error 10 and the VMM exited 33 while the fixed GLES probe
was starting. No nonce-bound readback or matching test PNG was captured.
**Boot completion and EGL initialization are not a successful GPU frame test.**
The next bounded comparison changes allocation order to normal-zone-first,
retaining 2 MiB alignment and the same kernel/GPU configuration.

The guest vendor DLKM loader still requests old loadable modules, which the
built-in-only kernel refuses. Its diagnostic abort is retained as an integration
issue; do not call this a clean, production-ready Android profile. The original
Android disks retained their hashes, both policy restorers succeeded, all six
values returned to `never`, and the host boot/core processes/trusted VM stayed
unchanged with taint 4608 and SELinux Enforcing.

After successful compilation, the owned Docker work volume/container were
removed and the earlier Mac source/object intermediates were cleaned. The Mac
cleanup freed about 2.49 GiB; the Image, final config, System.map, exact compressed
source, disassembly and logs/provenance remain private. No unrelated cache or
project was pruned.

## Trial 9: normal-zone-first, later buffer allocation failure

With the same custom guest kernel and GPU settings, the aligned normal-zone-first
comparison again reached BOOT=1 and guest Adreno EGL. The signed GLES probe
started. No Gunyah PAGE_FAULT attempt error was recorded during the bounded
160-second observation; the manager ended on its deadline (124). This is not
a long-running RAM stability claim.

SystemUI and Launcher repeatedly aborted with
`drawRenderNode called on a context with no surface!`. Their preceding errors
were `GraphicBufferAllocator` allocation error `-129` and `EGL_BAD_ALLOC`, even
for a 1×1 scratch buffer. Guest Mesa reported invalid virtio-GPU GET_CAPS
requests. No matching probe result/PNG was produced. A SystemUI PID or BOOT=1
alone must not be described as a usable Android desktop.

The examined upstream
[minigbm virgl backend](https://android.googlesource.com/platform/external/minigbm/+/refs/heads/main/virtgpu_virgl.c)
selects gfxstream's buffer-format capabilities through the gfxstream Vulkan
capset. The GLES-only developer mask omitted that capset. Standard
[Cuttlefish gfxstream configuration](https://android.googlesource.com/device/google/cuttlefish/+/refs/heads/main/host/libs/vm_manager/crosvm_manager.cpp)
includes GLES, Vulkan and composer context types. The gfxstream capset-fill
implementation can report format support from the GLES FrameBuffer even when
Vulkan emulation is absent.

A separate `--buffer-capset` comparison exposes that capability information
while leaving `vulkan=false`, the guest GLES renderer and host configuration
unchanged. It is a diagnostic hypothesis, not a claim that the format query
is already the confirmed cause of allocation error -129. Read-only guest
diagnostics now include a bounded buffer-allocation log filter.

## Trial 10: capset selection overrides the Vulkan parameter

The capset-only comparison retained the explicit `vulkan=false` argument but
**did not keep the backend Vulkan initialization disabled**. In this pinned
crosvm/rutabaga implementation, the selected capset mask calls `use_vulkan`
again and overrides the earlier argument. The owned VMM aborted (SIGABRT,
manager result 29) while looking up `vkCreateInstance` and two global enumeration
commands. The guest did not reach adbd or boot completion. Both policy restorers
and the host/disk boundary checks succeeded.

The builder metadata/help now distinguish the requested parameter from the
actual mask-derived backend selection. The failed comparison is retained; do
not describe it as format metadata exposure without Vulkan initialization.

The next separately selected `--loader-compat` comparison reuses the existing
process-local Android Vulkan-loader bridge for the owned source-built VMM.
It retries only those three global lookups with a null instance when the
instance-qualified lookup returns null. The exact compatibility library is
pinned by the manager/proxy; its constructor checks the owned VMM inode/name
and removes LD_PRELOAD from its environment. It does not edit the Android
loader, driver, namespace/SELinux policy or protection mechanisms, and it
contains no memory, mmap, Gunyah-ioctl or folio interposition. Plain GLES and
production launch paths do not use it. This is still a GLES guest workload,
with Vulkan backend initialization for the full capability set.

## Trial 11: loader repaired, Vulkan test allocation still fails

The pinned process-local loader bridge initialized and logged successful repairs
of the three global lookups. The full-capset comparison passed the earlier
missing-function failure and selected the physical host renderer, but gfxstream
then aborted while establishing the memory type for a 64×64 RGBA8 test
ColorBuffer. The log reported a zero-byte memory requirement and
`vkAllocateMemory: VK_ERROR_INITIALIZATION_FAILED`. The owned VMM received
SIGABRT (manager result 29) before guest adbd/boot completion. This is a later
host-renderer/Vulkan compatibility failure, not the earlier guest EGL version
reply or a recorded Gunyah PAGE_FAULT. The zero requirement is an observed
value to investigate, not a reason to replace a real allocation with fake data.

No new GPU success is claimed. Plain GLES trials 8/9 remain the evidence for
Android boot-complete and guest EGL initialization; their unusable/crashing
window surfaces remain unresolved. The original Android disks, host boot,
core processes and unrelated trusted VM were unchanged after trial 11, with
taint 4608, SELinux Enforcing and all six policies restored. The released APK
and its existing selectable profiles were not changed.

The follow-up priorities are the exact guest allocator/mapper failure behind
error -129, the gfxstream/Adreno test-image requirements and the actual
capset-derived renderer flags. Preserve these negative comparisons and avoid
blind VM retries, fictitious formats/requirements or silent feature enabling.
The 177 Python/native tests pass, including compile-time isolation checks for
the optional loader bridge. Built binaries and device logs remain private.

## Trial 12: real renderer feature override separates allocation failure

The source supports `renderer-features=Vulkan:disabled` through
`STREAM_RENDERER_PARAM_RENDERER_FEATURES`, applied after mask-derived flags.
Unlike the earlier boolean alone, this comparison reached BOOT=1 and guest
Adreno EGL with full format capsets and without the loader bridge or the
observed Vulkan initialization abort. Guest minigbm reported capset mask 776
(GLES, Vulkan-format and composer IDs), and the fixed GLES test app started.
No Gunyah PAGE_FAULT attempt error was recorded during the 160-second window.

The app/SystemUI window allocation still failed with -129 and no nonce-bound
result or matching PNG appeared. This rejects the simple claim that exposing
the format capset by itself fixes the window problem. The manager ended at
its deadline (124), and the full host/disk/policy postflight passed again.
The configuration is developer-only and not a new production app setting.

An important error-code distinction: Android 17's
[Binder Status](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android17-release/libs/binder/include/binder/Status.h)
defines `EX_TRANSACTION_FAILED = -129`. Its
[Gralloc5 allocator](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android17-release/libs/ui/Gralloc5.cpp)
returns a failed allocator call's exception code. Therefore the numerical
allocation error must not be diagnosed as Linux EKEYREJECTED/key authentication
just because `strerror(129)` says that, nor automatically as lack of RAM.
Further diagnosis must capture the underlying Binder transaction status and
allocator/mapper service logs in the actual guest; the current records do not
yet isolate that call's failure source.


## Trial 13: Binder failure is a denied metadata FD

A focused read-only comparison kept trial 12's configuration. Android again
reached BOOT=1, but SystemUI repeatedly crashed and no verified test frame was
produced before the 160-second limit (124). The graphics allocator service was
alive and registered. Binder reported `BR_FAILED_REPLY`, status **-1 (EPERM)**.
Guest SELinux audits denied application `read/write` access to allocator-created
`memfd_file` objects labelled `hal_graphics_allocator_default`. The decoded
names identify gralloc's **reserved-region metadata**, including window and
AHardwareBuffer allocations. This explains a concrete failed FD-transfer path;
it does not prove that every graphics failure has the same cause.

Android 17's [minigbm allocator source](https://android.googlesource.com/platform/external/minigbm/+/refs/heads/android17-release/cros_gralloc/cros_gralloc_driver.cc)
first tries the DMA-BUF system heap for this metadata when built with
`HAS_DMABUF_SYSTEM_HEAP`, then falls back to `memfd_create`. Its
[build defaults](https://android.googlesource.com/platform/external/minigbm/+/refs/heads/android17-release/Android.bp)
enable that feature for the generic platform. Both the original extracted
configuration and our first custom guest disable `CONFIG_DMABUF_HEAPS_SYSTEM`,
although the general heap framework is enabled.

The next isolated guest-only kernel enables this driver, preserving the DMA
sync fix and existing security configuration. This tests the intended allocator
route rather than adding SELinux allow rules, changing labels or suppressing
Binder errors. The guest's actual heap device, label/access, remaining denial
logs, test readback and screenshot must still be checked. No success is inferred
from the configuration change or compilation alone.

Trial 13's host postflight again retained the same boot, core processes,
unrelated trusted VM, original Android disk hashes, taint 4608, Enforcing and
all six shmem policies at `never`. The public helper adds bounded read-only
Binder/heap diagnostics and a fixed archive option for this failed comparison.
