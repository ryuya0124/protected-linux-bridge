# Android 17 GPU / 1536 MiB research

Status: 2026-10-19, preview.7. **Built; on-device startup refused by the host health
gate. No Android hardware-rendered home frame has been verified.** This source
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
The current session refuses before preparing this directory; no copying or
mutation of the existing Android/Debian disks was done for this test. A future
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

## Actual on-device result

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
validate this new prefault-only/GPU combination. Continue source/build work in
this session; a new VM load requires a clean permitted session. Do not clear
kernel taint, bypass the guard, load a host module or restart Android to obtain
an apparent result.

[Sanitized checks](data/android-preview7-checks-2026-10-19.json) ·
[App RAM selections](android-large-profile.md) ·
[Memory failure and retired operation](memory-increase-2026-10-19.md) ·
[Host boundary](host-boundary.md)
