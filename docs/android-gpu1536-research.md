# Android 17 GPU / 1536 MiB research

Status: 2026-10-19, after preview.7. **The separately approved one-time trial
stopped at prefault-only backing admission, exit 89, before the Android guest
booted. No Android hardware-rendered home frame has been verified.** Normal
diagnostics and the released APK retain their BAD_PAGE/OOPS guards. This source
is a developer diagnostic, separate from the released app's software renderer.

## Why move the GPU comparison to 1536 MiB?

A retained 512 MiB GLES trial logged all of the following in the same run:

- gfxstream's graphics adapter was `Android Emulator OpenGL ES Translator
  (Adreno (TM) 840)` and its renderer initialized successfully.
- The guest's `surfaceflinger` context opened `pipe:opengles` and submitted fences.
- The guest then reached `Out of memory and no killable processes` and panicked
  in its own `surfaceflinger` context at about 10.49 seconds.

This establishes host renderer initialization and guest protocol activity, not
an Android GPU-rendered home screen. The host display's `unsupported by the
implementation` message did not itself terminate that run: the guest continued
opening rendering contexts afterwards. Bridge's separate guest PNG path may
avoid relying on that display, but its hardware-rendered output remains to be
verified. The 512 MiB shortage is one observed blocker; increasing RAM does not
prove that the remaining graphics/HAL/transport paths work.

## New bounded 1536 MiB GLES diagnostic

Build without installation, VM launch, host driver loading or policy changes:

```sh
python3 tools/build_android_gpu_probe.py --sdk "$ANDROID_HOME" --memory 1536 --mode gles
```

Output stays in ignored `artifacts/android-display/gpu-research/gles1536/`.
Mac free space must exceed 21 GiB before building. The builder produces:

- `android-gpu1536-gles-runner`: fixed 1536 MiB, two CPUs, 480×800, gfxstream GLES
  and composer contexts, Vulkan disabled. It pins the exact Galaxy host build,
  kernel, SELinux Enforcing, original VMM digest, guest kernel and GPU initramfs.
- `libandroid_gpu1536_gles_backing.so`: separate SHA-pinned, process-scoped shared
  backing. Exact VMM inode, name, three disk paths, renderer, CID, RAM argument,
  initrd and 1504 MiB main memfd size are required. The 64 MiB base-page margins
  and full middle-range THP admission remain. No mapped hole punching or ioctl
  interposition is compiled into this variant. It also checks host health itself.
- A policy supervisor with the fixed developer stage `--android-gpu1536-gles`.
  It retains the common owner lock, only the previously approved three small-shmem
  nodes, ten-second normal restoration, independent forty-second deadline,
  2304 MiB MemAvailable admission and tracked-child cleanup. Before the first
  policy write it also runs the pinned diagnostic preflight under a ten-second
  deadline and refuses an app freezer cgroup. No silent app opt-in
  or persistent policy change is introduced.

Its intended disk directory is the **separate** private `cf17-gpu1536-gles`.
The original preview.7 check refused before preparing this directory; no copying
or mutation of the existing Android/Debian disks was done in that check. A future
clean-session preparation must verify an offline copy under the owner lock and
use the builder's pinned `android-gles.cpio`, not overwrite any existing profile.
The builder does not prepare/install disks, and the runner refuses missing files.
Do not manually bypass those checks to make it run.

The guest identity-checked observer is the single owner of swap/bootstrap setup
in this combined build. It records boot completion, a private screenshot and
SurfaceFlinger's actual `GLES:` renderer line. Console observation remains separate
without a second concurrent swap initializer. Properties, VMM zero exit or a
host renderer string alone must never be accepted as proof of hardware drawing.
A 1536 MiB Vulkan combination is explicitly rejected for now; the older isolated
512 MiB Vulkan loader diagnostic still builds independently.

## Original preview.7 refusal, before the one-time approval

The diagnostic executable/library and supervisor were installed in app-private
research storage. Both `--check` and a start request returned **42** with
`host_memory_warning=1`. The supervisor's new stage returned **38** before a
policy write. The host remains in the already affected boot, with BAD_PAGE taint.
No new VM, pressure allocation or GPU driver operation was started. The new disk
directory remained absent, three policies stayed `never`, SELinux stayed
Enforcing, and host boot/main system processes and the unrelated VM were unchanged.
No Android or software reboot was performed.

This is verification of refusal, **not a failed/successful GPU boot experiment**.
The old 1536 MiB software screenshots used the retired runtime; they do not
validate this new prefault-only/GPU combination. At that point only source/build
work continued. The later single-trial exception is recorded below. Do not clear
kernel taint, bypass the guard, load a host module or restart Android to obtain
an apparent result.

## Explicitly approved one-time follow-up

After the owner confirmed that Android remained usable, they explicitly accepted
the remaining host-stop/automatic-restart/root-loss risk and approved **one**
1536 MiB GLES trial in the existing BAD_PAGE-marked boot. Usable Android UI is
not proof that the earlier kernel warning has been repaired. Conversely, a sticky
historical taint alone does not establish a continuing failure. The approval did
not authorize clearing taint, a host restart or weakening ordinary builds.

The exception is compiled only into a distinct developer artifact. It requires
the exact private boot UUID, a five-minute CLOCK_BOOTTIME window, the exact
existing taint value, and an inherited FD matching a root-owned, single-link
0600 marker created with `O_EXCL`. The marker is retained even after failure.
Standalone `--check` without that FD still returned 42. A missing monitor,
changed boot/taint, expired window or existing marker refuses the exception.
The build rejects other RAM sizes, Vulkan, an absent fixed APK or incomplete
approval fields. This is an operational one-trial guard for a trusted developer
with root, not an authorization boundary against a malicious root user.

Before claiming the trial, an offline helper held the common owner lock, copied
and byte-hash-verified the original three Android writable disks into the separate
`cf17-gpu1536-gles-once` directory, and installed the pinned GLES initramfs there.
The original disks and existing Debian profile were not replaced. The original
and private VMM copies still had the expected digest.

The supervisor opened `/dev/kmsg` read-only, sought to its end and watched new
BUG/Oops/panic records throughout its bounded owned-child wait. Read loss or
monitor overflow also stops the owned trial. The three previously approved
shmem settings, normal ten-second restoration, independent forty-second
guardian and existing tracked-child cleanup were retained. No mapped hole
punching, refault fallback or ioctl interposition was compiled into the backing.

### Measured result

| Measurement | Result |
|---|---|
| Configured RAM / reserved SWIOTLB | 1536 MiB / 32 MiB |
| Main original shared memfd | 1504 MiB; prefault completed |
| Base-page boot margins | 64 MiB prefix and 64 MiB tail |
| Inspected middle | 1376 MiB; all 352256 pages present, no hidden PFNs or flag-read errors |
| Middle pages with THP flags | 25248 / 352256, **7.17%**, covering 98.625 MiB |
| Sampled full 2 MiB folios | 0 |
| Admission | `ANDROID_BACKING_UNSUITABLE`, VMM and supervisor exit **89** |
| Android guest / GPU render test | Not reached; no new guest frame |
| Restore | Controller and independent guardian both acknowledged success; all three settings `never` afterwards |
| Host boundary | Same boot and main system processes, SELinux Enforcing, unrelated trusted VM unchanged |
| Original Android disks | All three byte hashes unchanged |

The library exited while returning the main RAM mapping to crosvm, before its
admission marker or guest boot. This is neither a new Gunyah ENOMEM measurement
nor a gfxstream failure. It demonstrates insufficient observed THP coverage for
our conservative existing gate. It does **not** prove that a 7.17% mapping would
necessarily fail in Gunyah, and we did not remove that gate to try.
There was no new kernel-failure monitor report; a bounded post-trial kernel tail
also contained no BUG/Oops/panic matches. This does not certify the already
affected host as repaired or safe for another load.

A read-only post-trial buddy snapshot showed **zero free blocks of order 4 and
above** in the exposed Normal zone: no immediately free blocks of 64 KiB or
larger at that instant. Smaller blocks remained available. This supports a
fragmentation/high-order-availability concern, but is a post-release snapshot,
not a measurement of every failed fault. The shmem statistics were cumulative
host-wide counters; no before/after delta was collected for this run, so they
cannot identify which allocation attempts belonged to it.

The related [shmem allocation source](https://github.com/torvalds/linux/blob/v6.12/mm/shmem.c)
tries suitable enabled large orders and falls back to a base-page allocation
when they cannot be supplied. The
[THP documentation](https://docs.kernel.org/admin-guide/mm/transhuge.html)
also distinguishes advice from actual large-page availability. This agrees with
the observed successful ordinary prefault and sparse THP coverage. Samsung's
allocator and vendor hooks were not verified instruction-for-instruction, so the
reference code is an explanation to investigate, not proof of the exact cause.
Changing the guest kernel or GPU flag cannot directly manufacture contiguous
host folios. No global compaction/defrag/cache-drop change, extra pressure cycle,
host module or retry was performed. The single-use marker remains consumed.

[Sanitized one-trial measurements](data/android-gpu1536-once-2026-10-19.json).

## Fixed guest rendering proof, built but not yet exercised

`guest-gpu-probe` is a small signed, non-debuggable APK with no requested
permissions or launcher entry. Before creating EGL it requires the pinned
Cuttlefish device/hardware/build and a 128-bit per-launch random nonce. It is
not installed on the Galaxy host. Its source and GLSL assets remain readable;
only its release binary is minimized to keep the embedded upload bounded.

The optional `--guest-render-apk` build argument authenticates the fixed package,
signing certificate, non-debuggable manifest, no permissions and 64 KiB size
limit. The generated upload repeats guest identity/root checks, uses only the
owned guest's AF_VSOCK ADB endpoint, verifies payload length/hash, installs only
that APK and removes only its acknowledged temporary files. It is not a public
ADB upload endpoint and never opens host TCP ADB or shares USB/storage.

After guest boot, the test draws two phases of GLES quadrants with a white
center, checks actual `glReadPixels`, and renders the full random nonce as a
32-stripe texture. The observer records the result and PNG, then attempts the
home screenshot and an identity-checked **guest-only** orderly poweroff. Neither
properties, host renderer strings nor exit zero are sufficient evidence.

Offline validation, requiring the matching nonce, actual Adreno renderer,
successful readback in both phases, PNG integrity/size and matching visible
colors/nonce, is available with:

```sh
python3 tools/verify_guest_gpu_frame.py --log RESULT.log --png FRAME.png --nonce NONCE
```

Software renderer names, stale phase/nonce, malformed PNG and ambiguous results
are rejected. CPU-generated unit-test images validate the verifier only; they
are **not device GPU evidence**. Even a future passing test-app frame would not
alone establish Android home GPU rendering or sustained system stability.

All 143 Python/native tests, the two guest JVM identity tests and guest release
Lint/build passed. A normal guarded 1536 MiB render diagnostic also builds.
The APK's normal guarded release remains preview.7; no experimental exception
was added to that release.

[Original sanitized checks](data/android-preview7-checks-2026-10-19.json) ·
[App RAM selections](android-large-profile.md) ·
[Memory failure and retired operation](memory-increase-2026-10-19.md) ·
[Host boundary](host-boundary.md)
