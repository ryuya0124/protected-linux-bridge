# Android guest display investigation

Status: 2026-10-19. The release APK still provides Debian profiles. **An interactive Android guest is not implemented or verified.** No Android image is flashed or installed into the host. All downloaded images are ignored development artifacts; phone-side files are confined to Bridge's private data. The existing Debian profiles/disks and stock kernel are retained. [Host prohibitions](host-boundary.md) remain in force.

## Current boot gates

| Candidate | Verified | Remaining gate |
|---|---|---|
| Existing Android 16 SDK / 4 KiB kernel | Diskless kernel boot/poweroff; separate SDK disks reach second-stage init, apexd, servicemanager and system_server attempts | No working SurfaceFlinger; Goldfish emulator HAL/topology mismatch, fresh filesystem feature mismatch |
| Latest checked SDK 37.2 / Android 17 / 16 KiB | Official repository metadata, bounded kernel/ramdisk download, kernel configuration inspection | No console output in bounded diskless probe; no successful boot claimed |
| Android 17 Cuttlefish / 4 KiB | Diskless memory pass; separate GPT/qcow disk reaches normal guest AVB/verity mounts, guest SELinux, APEX, BootControl, encrypted F2FS userdata, HWC, SurfaceFlinger and system_server | Additional virtual HAL dependencies; no verified frame capture, interactive display, input or complete boot |

Cuttlefish build `16373615`, target `aosp_cf_arm64_only_phone-userdebug`, identifies Android 17 `CP2A.260605.016`. Its boot image contains Linux `6.12.74-android16-6-g3ec022196c4e-ab15076761-4k`. The kernel release name does not mean its Android userspace is version 16. The matching vendor ramdisk includes virtio-gpu, virtio-input, virtio-block, virtio-net and vsock modules. These are **guest modules**; none are loaded on Android's host kernel.

## Why change image family

The SDK vendor contains Goldfish/ranchu emulator graphics libraries and expects services such as qemu-props/Goldfish pipes. Enabling a kernel option or adding a virtio GPU does not supply those host protocols. The Cuttlefish vendor inspected offline contains minigbm and corresponding virtual-device init configuration. This is a more appropriate candidate for crosvm, not a guarantee of compatibility.

[Official Cuttlefish graphics documentation](https://source.android.com/docs/devices/cuttlefish/gpu) distinguishes host acceleration and guest software rendering. The [published crosvm manager graphics configuration](https://android.googlesource.com/device/google/cuttlefish/+/e67261ab6e270374f6440b1845f55a8afdd764f4/host/libs/vm_manager/crosvm_manager.cpp) supplies different HAL properties for guest SwiftShader and VirGL. Those values must be checked against the downloaded vendor before a launch; flags are not proof of an active renderer. The installed VMM's direct GPU probes previously reported an unsupported host display, so getting SurfaceFlinger running and getting its output into Bridge are separate gates.

No HAL/policy/verity restriction has been removed to force a GUI. Software rendering may be needed to establish a baseline before GPU work, but the [memory-provisioning failure](gunyah-memory-investigation.md) can occur with no GPU at all. A complete Android boot should not be attempted at arbitrary large RAM settings on the assumption that more memory is always safer.

## Bounded offline preparation tools

```sh
docker build -t plb-fs-tools:trixie - < tools/android-fs-tools.Dockerfile
python3 tools/fetch_android_guest.py
python3 tools/fetch_android_guest.py --include-super
python3 tools/inspect_android_guest.py
python3 tools/inspect_android_guest.py --extract vendor_a
python3 tools/build_android_probe.py
```

These operate only on regular development files. They do not access ADB, mount filesystems, run a host launch package or start any VM. The pinned public CI archive/members are recorded in `third_party/android-cuttlefish17.json`. Downloads require honored HTTP ranges, exact sizes, CRC32 and recorded SHA256. The whole-archive MD5 is provenance metadata, not an asserted verification of a selectively downloaded archive. The large super member now also has a recorded SHA256.

The sparse image's logical size is 8 GiB, but its downloaded representation is about 1.63 GiB. `android_sparse.py` reads raw/fill/zero ranges without first expanding that entire image, validates geometry/header/table SHA256 checksums, and bounds logical extents. Unknown sparse types, unsafe names, multi-device layouts, invalid checksums and out-of-range extents fail closed. The format reference is [AOSP LP metadata](https://android.googlesource.com/platform/system/core/+/a3b721a32242006b59cb12bd62c9133632af3a2d/fs_mgr/liblp/include/liblp/metadata_format.h). Selected logical images are extracted to new files; existing outputs are preserved.

The build step additionally requires Python 3.11+, `qemu-img`, `lz4` and the development filesystem container above. It combines the unchanged guest super/boot/VBMeta/factory-userdata payloads with a fresh metadata filesystem in a new GPT container, converts it to qcow2, verifies raw/qcow contents agree, then removes only its temporary raw expansion. A separate writable overlay preserves the readonly factory base. Guest `misc` is another new GPT data file, not an Android host block device. Existing outputs are refused rather than overwritten. This is developer preparation, not an APK import feature.

The fetch/extract tools retain at least 20 GiB free on the Mac. After disk preparation, about 4 GiB of new Android-display investigation artifacts were present, with roughly 41 GiB free. No full AOSP/LineageOS checkout or host package download was needed. Kernel and OS image binaries, private logs, ADB identities and keys are not committed or bundled in the APK.

## Matching userspace probe

The fixed-path `android_probe_runner.c` acquires the same profile/storage lock and starts only an owned 512 MiB/one-CPU VM, initially with software virtio GPU at 480×800. It bounds execution to 45 seconds plus termination grace and signals only its tracked child. The factory base, disposable working overlay, guest-only misc disk, initramfs, logs and guest kernel all live under Bridge's private data. Debian disks are never attached. A missing file, busy lock or insufficient memory refuses the launch.

The first guest AVB failure was missing bootloader summary metadata. It was corrected by computing the real digest and size of the original VBMeta chain, **including AVB footers in boot and init_boot**. The pinned total is 22912 bytes; both the build tool and provenance manifest record it. No verity/verification-disable flags, unlocked-device flags or fake green boot state were introduced. Guest signature/hashtree checks remain in the normal init path. This does not create pvmfw attestation or a hardware-rooted boot chain. Host Android's AVB is a separate, unchanged mechanism. See [the userspace AVB verifier](https://android.googlesource.com/platform/system/core/+/a3b721a32242006b59cb12bd62c9133632af3a2d/fs_mgr/libfs_avb/fs_avb.cpp).

The downloaded vendor includes multiple variants of Gatekeeper, KeyMint, composer and camera APEXes. Selecting its existing software/nonsecure guest Gatekeeper/KeyMint and ranchu composer let bootstrap continue without a remote/Trusty security VM. These are emulated **guest** security services, not hardware-backed keys or attestation. No host security service, policy or unrelated trusted VM was modified.

The initial ext4 fstab selection did not match the factory userdata image. Its actual superblock is F2FS; switching to the corresponding supplied fstab enabled normal encrypted F2FS userdata initialization. A missing guest `misc` device had independently prevented BootControl from registering; a separate new guest-only disk resolved that dependency. Neither correction modified the signed system/vendor/boot images or an Android host partition.

At the recorded 45-second sample, compositor services, SurfaceFlinger and a system_server process started, while missing virtual HAL services still prevented completing startup. The installed crosvm also reports that its native host display is unsupported. **Service startup is not a verified GUI frame or an interactive Android boot.** No Android profile switch is exposed in the release APK yet.

Providing a guest vsock device and the actual `vsock_lights_port` boot property let the existing guest Lights service get past its dependency; [its published implementation](https://android.googlesource.com/device/google/cuttlefish/+/a1162ca7a4e6297f1699b65052a8c2dd466fd518/guest/hals/light/lights.rs) starts a guest-side vsock server. It does not require changing the Galaxy's brightness service. The following gate was the absent virtual sensors service. In that 512 MiB sample, guest Linux then OOM-killed system_server at about 41 seconds. This is **guest RAM exhaustion**, separate from the hypervisor `NOMEM` failure in larger diskless memory probes. No Gunyah mapping fault was recorded in that Android sample. Raising RAM is not assumed to solve both problems.

## Next integration requirements

1. Prepare a separate, checked guest-only disk/ramdisk layout with matching fstab and AVB images. Preserve guest image verification and distinguish it from host Android AVB. Fresh userdata must use filesystem features supported by that guest's tools.
2. Verify first-stage mounts, matching guest modules, guest SELinux policy, SurfaceFlinger/composer, then actual frame output. An init/service start or VMM exit zero does not establish Android GUI support.
3. Connect an authenticated, bounded frame/input path to Bridge. Physical keyboard/mouse, focus release, rotation, resource limits and VM-local teardown need the same scrutiny as Debian's viewer.
4. Only after those tests expose a separate Android profile in the APK. Debian fallback must remain possible, and stopping it must never signal Android or an unrelated VM.

LineageOS/GSI userspace alone does not supply the matching vendor/HAL/device stack. The Android 17 Cuttlefish candidate moves the baseline forward without promising that arbitrary phone ROM images will boot.
