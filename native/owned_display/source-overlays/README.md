# Android 17 developer source overlays

These patches describe the separate app-private prototype used for the device
checks in [the validation record](../../../docs/android17-runtime-validation.md).
They do not patch the installed VMM, kernel, APEX or host libraries. They do not
enable Android guests in the APK. No VM-start command is provided here.

Use fresh source trees named `crosvm-source`, `gfxstream-source`,
`rutabaga-source` and `minijail-source` under one private build directory. Exact
upstream revisions and patch hashes are in `sources.json`:

- [AOSP crosvm](https://chromium.googlesource.com/crosvm/crosvm/+/c7dd8d6950912fa5e6fde7ec2d1616e0ddc090f8), BSD-3-Clause.
- [AOSP gfxstream](https://android.googlesource.com/platform/hardware/google/gfxstream/+/b46f0d4b4545def09bdf30651d4bf848b2ba0c34), Apache-2.0 and retained upstream notices.
- rutabaga_gfx 0.1.76-alpha.0 registry archive, BSD-3-Clause; verify its archive hash.
- [AOSP minijail](https://android.googlesource.com/platform/external/minijail/+/c6fe710936b84753b99e6d0e9ea634109272f307), retained upstream BSD-style license.

Apply each patch only to its corresponding pinned tree, with `git apply --check`
first. Copy `tools/crosvm_preallocated_ram.rs` from this repository into
`crosvm-source/vm_memory/src/guest_memory/plb_adopt.rs`, and
`native/owned_display/ahb_export.rs` into `rutabaga-source/src/plb_ahb.rs`.
Replace only the source tree's existing minijail symlink with a relative link to
that pinned sibling checkout. Never replace a device library or kernel.

The prototype uses Rust 1.90.0 and NDK 28.2.13676358, target
`aarch64-linux-android`, API35 for native components and SDK37 for the app/test.
Build gfxstream using its Android CMake toolchain and EGL-on-EGL path. CMake
requires `PLB_ANDROID_NATIVEWINDOW_LIBRARY` and `PLB_ANDROID_CUTILS_LIBRARY` link
inputs. Those and minijail/cap/processgroup inputs were obtained read-only from
the exact inspected host. They are not distributed here. NDK/VNDK header and
library equivalence is still a portability limitation, not a promise of support
on another build. Preserve the upstream CMake-generated include list when
compiling the C++ renderer probe; its RenderThreadInfo requires host/vulkan
headers even though this test does not initialize Vulkan.

The private Cargo build uses no default features, with
`gunyah,plb-preallocated-ram,gpu,gfxstream,qcow,android-sparse,android_display`.
Provide the real owned-display backend link directory through
`PLB_DISPLAY_BACKEND_DIR`, gfxstream through `GFXSTREAM_PATH` and native
cross-build pkg-config metadata. Build the cdylib; do not install it over stock
crosvm. `plb-preallocated-ram` deliberately has **no fresh-allocation fallback**:
its caller must supply the exact, size-sealed FD32/identity contract. CPU probe
entry and Android launch entry are different callers.

## GLES AHB ownership

The GLES-only RGBA8 ColorBuffer has real AHardwareBuffer/EGLImage-backed texture
storage. Unsupported export formats return no AHB; no fake handle or implicit
Vulkan fallback is used. Reformatting/import paths must not orphan the exported
backing. This prototype has not qualified every format, snapshot or import path.

The ColorBuffer retains one allocation reference. A descriptor retains a shared
owner, and each resource export acquires a separate reference for the consumer.
The Cargo helper consumes/releases that export reference, including on metadata
validation failure, after duplicating its FDs. The external C API is unchanged;
all renderer-internal C++ components must be rebuilt together after the added
owner field. The no-VM renderer probe checks GL drawing, normal window flush,
AHB export and actual presentation; it is not a guest GPU protocol or asynchronous
buffer-reuse/fence test.

The forced renderer feature `Vulkan:disabled` is mandatory for this GLES recovery
work. Upstream dispatch loading may still load libvulkan; do not confuse that
with Vulkan-device creation or a successful Vulkan guest. The earlier dangerous
Vulkan/crash runners stay disabled.

The sources under `tools/owned_display_probe` require per-build generated pins,
fixed arguments and a same-signer test APK. They are developer scaffolding,
not a portable runtime installer. Missing generated input must fail the build;
never remove identity/pin/owner/health/guardian checks to make it run. Keep a
source-and-binary manifest and reject stale binaries after a failed build.

## Separate native YUV diagnostic overlay

`gfxstream-native-yuv-experiment.patch` is an additional, **off by default**
experiment, not part of `gfxstream-android17.patch`. Its hash is separately
recorded in `sources.json`. Build all components into a new directory with
`PLB_GFXSTREAM_NATIVE_YUV_EXPERIMENT=1`; never replace the established runtime.
It adds independent external texture state and permits native GLES external
image/FBO paths. It leaves the extension whitelist unchanged, and does not
implement the guest encoder or writable ColorBuffer transport. Internal state
layout differs: do not mix libraries or snapshot formats. See the actual native
and translated YUV plane/sampler tests and remaining requirements in
[the YUV record](../../../docs/android17-yuv-target.md).
