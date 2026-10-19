# Android 17 at 4 GiB: RAM and GPU investigation

Updated 2026-10-20 JST. Work is ongoing. **No Android boot-complete or
hardware-rendered guest frame has been verified in these trials.** The released
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

Each VM used 4096 MiB, two CPUs, the same pinned guest kernel/ramdisk, a fresh
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

All five trials ended in the same host boot, SELinux Enforcing, taint 4608 with
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
