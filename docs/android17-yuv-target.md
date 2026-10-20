# Android 17 YUV-target rendering investigation

2026-10-21. This is an offscreen developer experiment, **not a camera-support
release or a complete GL_EXT_YUV_target implementation**. The normal VM renderer
and production readiness gate are unchanged. No VM was started in this round.

## What failed, and what now passes

The guest's stock VirtualCameraService refuses registration because its GLES
implementation does not advertise GL_EXT_YUV_target. That does not mean that
the host Adreno lacks the extension. A native host probe on the inspected
Android 17 Galaxy found it and rendered actual YUV hardware buffers successfully.

The pinned gfxstream translator has several independent gaps:

- Its extension whitelist does not include YUV_target.
- External texture bind/image/FBO validation rejects the external target, and
  context texture-binding state has no independent external slot.
- The guest GLES encoder rewrites external samplers and texture binds to 2D;
  simply adding an extension name on the host cannot fix that protocol.
- ColorBufferGl represents YUV resources internally as RGBA plus a conversion
  cache. This differs from a native writable YUV EGLImage and needs coherent
  producer/readback handling before a camera can use it.

A separate, compile-time experimental overlay adds an external texture state
slot, context binding restoration and native GLES external-image/FBO validation.
The extension whitelist is deliberately unchanged. The diagnostic calls the
emulated GLES dispatch directly, below the unmodified guest encoder, and imports
a real YUV AHardwareBuffer through the translator's EGL path.

| Actual device comparison | Result |
| --- | --- |
| Native host GLES / Adreno 840 | Eight positive rendering/sampling checks pass |
| Existing translator without the new overlay | Rejects the YUV framebuffer before drawing, exit 30 |
| Separate translator with the overlay | Eight positive rendering/sampling checks pass, exit 0 |
| Full guest encoder + ColorBuffer + VirtualCameraService | Not connected or qualified; camera blocker remains |

Each successful probe uses a 16×16 synthetic YUV420 AHardwareBuffer, not a camera
frame or host screen. Five GPU-output cases verify 384 actual Y/U/V components
each: a constant, red converted using BT.601 limited/full and BT.709, and an
out-of-RGB-gamut YUV value. The raw YUV sampler then preserves that last value
without an RGB clipping round-trip. YUV-to-RGB and ordinary RGBA regression checks
also pass; these three readbacks verify 1024 components each. Total: 4992 checked
components, no mismatches within the documented quantization tolerances.

Independent 2D/external texture bindings and translator context unbind/rebind are
also checked. These positive cases are **not full extension conformance**:
negative draw-state validation, framebuffer/snapshot/import coverage, cross-context
synchronization and the complete guest protocol still need work.

The first translator fixture omitted mandatory renderer callbacks. Its own
process crashed before a valid graphics test; this was fixed in the harness and
is not counted as a GPU or VM result. One postflight script also rejected a
transient extra process inheriting the zygote name. Follow-up metadata confirmed
the original core processes, boot, taint and enforcement; final comparison pins
the existing core processes rather than requiring an unchanged global pidof list.

## Reproduce builds without running a device

Start with the exact gfxstream revision and normal Android 17 overlay documented
in [source overlays](../native/owned_display/source-overlays/README.md).
Apply `gfxstream-native-yuv-experiment.patch` **in addition**, checking it first.
Configure a separate CMake build directory with
`-DCMAKE_CXX_FLAGS=-DPLB_GFXSTREAM_NATIVE_YUV_EXPERIMENT=1` and
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, using the same pinned NDK/platform link
inputs as the normal renderer. Rebuild all renderer components together.
The additional texture state changes internal layout/snapshot assumptions;
never mix those components with the normal renderer or use guest snapshots.

```sh
python3 tools/build_yuv_probe.py --sdk "$ANDROID_SDK_ROOT" \
  --output artifacts/yuv-probe
python3 tools/build_yuv_probe.py --sdk "$ANDROID_SDK_ROOT" \
  --output artifacts/yuv-probe \
  --renderer-build artifacts/gfxstream-yuv-build \
  --display-backend-dir artifacts/host-libs
```

The builder generates source/binary hash manifests and removes stale outputs on
failure. It never uploads, invokes root, starts a VM or modifies a device. Running
these host probes separately requires exact device identity, healthy host state,
the common maintenance/VM-owner leases and a bounded supervisor. The test creates
no Activity, display endpoint, ANativeWindow, camera stream or Vulkan device.
AHardwareBuffer GPU output is offscreen. Vulkan is explicitly disabled in renderer
initialization; loading the platform Vulkan dispatch library is not device creation.

Device postflight retained the same boot/core/trusted VM, taint 4608 without
BAD_PAGE/OOPS, Enforcing SELinux and all six original `never` shmem values. No
host tuning, compaction, kernel/module/partition/protection change or restart was
performed. The user permitted stopping the existing Debian bridge first; its
normal `--stop` returned 0 and its disks were retained. Raw logs/binaries remain
private and are not part of this repository.

## Remaining integration

Implement an explicitly negotiated native-external-texture transport in a
matched guest encoder/EGL stack, preserving ordinary 2D sampling and raw YUV
uniform semantics. Connect that to actual writable YUV ColorBuffers, plane
layouts, producer fences and guest readback. Only then qualify the complete API
and advertise GL_EXT_YUV_target to that compatible guest. Merely exposing its
name, bypassing VirtualCameraService's requirement, or RGB clipping/reconversion
would not implement the required behavior.

After this path passes, rerun synthetic virtual-camera registration, three-frame
ImageReader capture and revocation before connecting opt-in physical Camera2
capture. No physical camera or microphone has been opened by these probes.

References: [Khronos extension specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_YUV_target.txt),
[pinned gfxstream source](https://android.googlesource.com/platform/hardware/google/gfxstream/+/b46f0d4b4545def09bdf30651d4bf848b2ba0c34/),
[AOSP virtual camera](https://android.googlesource.com/platform/frameworks/av/+/refs/heads/android17-release/services/camera/virtualcamera/VirtualCameraService.cc).
