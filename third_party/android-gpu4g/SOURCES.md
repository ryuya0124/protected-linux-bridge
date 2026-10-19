# Optional Android 17 GPU4G runtime

The GPU-enabled APK adds a separate guest kernel and source-built userspace VMM.
It never replaces the Android host kernel or installed VMM. Missing optional
inputs omit this bundle; legacy guest profiles still build and run.

- crosvm: AOSP commit `8a4abfbaea5567de24e49cc43874c820098987d0`.
  Source: https://android.googlesource.com/platform/external/crosvm/+/8a4abfbaea5567de24e49cc43874c820098987d0/
  First-party patches and build recipe: `tools/build_preallocated_vmm.py` and
  the `tools/crosvm_*.patch` / `tools/crosvm_*.rs` files in this repository.
- Minijail source revision: `c6fe710936b84753b99e6d0e9ea634109272f307`.
  Source: https://android.googlesource.com/platform/external/minijail/+/c6fe710936b84753b99e6d0e9ea634109272f307/
- ARM64 Linux guest: Android common `3ec022196c4e9d5c1434599cdda63f622dd6f586`,
  6.12.74, GPL-2.0-only (see `third_party/linux/`).
  Corresponding source archive:
  https://github.com/aosp-mirror/kernel_common/archive/3ec022196c4e9d5c1434599cdda63f622dd6f586.tar.gz
  SHA256: `13ab707b031ace652b6d7d111dfe4f877e186e1bc0c80925ea19c34dc0c3e531`.
  Complete final configuration: `kernels/android-gpu/system-heap.config`.
  Patch, compiler/container and build recipe: `kernels/android-gpu/` and
  `tools/build_android_guest_kernel.py`. The compiler is Clang 19; the guest
  release is `6.12.74-plb-android-gpu2`. Source/configuration/build scripts are
  provided in addition to the binary's in-app copyright/license notices.

`DEPENDENCY_LICENSES.txt` retains upstream notices from the pinned crosvm
workspace and the source cache used to build it. The cache inventory is a
superset including build tools and optional dependencies; inclusion does not
claim each listed crate is linked into this particular binary. Upstream notice
text is unmodified. System-provided Android libraries/drivers are not bundled.

The APK includes only a small guest ramdisk overlay. At runtime it appends this
overlay to a hash-verified retained stock guest initramfs in the new profile.
It does not redistribute the Android OS disks or original initramfs. Guest disks
must be prepared separately using the existing documented Android guest setup.
The signed diagnostic GLES APK, raw logs/screenshots and private keys are not
included in the interactive application runtime.

A successful diagnostic readback is not a claim of reliable startup, smooth
frame transfer, zero-copy, Android networking/audio/USB or Vulkan support.
