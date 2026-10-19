# Android guest display investigation

## Larger-memory diagnostic, 2026-10-19

An isolated **1536 MiB Android 17 Cuttlefish guest** now reached
`sys.boot_completed=1`, started SystemUI and produced a verified 480×800 lockscreen
PNG. A second 1536 MiB run also completed boot; waking/dismissing the guest
keyguard and sending the guest HOME key produced a resumed
`com.android.launcher3/.uioverrides.QuickstepLauncher` activity and a valid home
screen PNG. In these approximately two-minute observations, guest MemTotal was 1468784 KiB,
SurfaceFlinger/SystemUI retained their observed PIDs and no vCPU crash was logged.
The fixed diagnostic ended at its time budget; this is not a long-term stability,
hardware-acceleration or full Android compatibility claim.

This result uses the user's narrowly approved, temporary **host-wide** 16/32/64 KiB
shmem exception and a separately compiled shared-memory diagnostic. All sampled
middle backing pages had THP flags before donation. The three host settings were
restored after the first ten seconds, under an independent 40-second guardian,
before the rest of Android boot. Host boot ID was unchanged and SELinux stayed
Enforcing. Earlier 1536/2048 MiB trials with base-page fallbacks still reproduced
the Gunyah map error; merely raising the RAM value is insufficient.

The guest's regular root-verified virtual-HAL/swap bootstrap is required. A first
read-only observation omitted it and never completed boot, even though it ran
without a vCPU fault for two minutes. The successful trial includes the same
bootstrap as the normal 512 MiB viewer. Host device nodes, host swap, partitions,
kernel and protection settings were not modified.

The writable Android qcow2/misc/FRP files are offline copies in a separate private
profile. Original writable-file SHA256 values matched before/after. Existing
Debian and the ordinary 512 MiB Android profile remain preserved; the released
APK does not apply these host settings or expose this diagnostic as stable.
Build-only entry: `tools/build_android_memory_probe.py`. Measurements and limits:
[approved small-shmem trials](small-shmem-policy-proposal.md),
[aggregate data](data/small-shmem-trials-2026-10-19.json).

Status: 2026-10-19. The release APK retains Debian profiles and adds a separate, limited Android 17 display preview. **A stable, usable Android desktop is not verified.** No Android image is flashed or installed into the host. All downloaded images are ignored development artifacts; phone-side files are confined to Bridge's private data. The existing Debian profiles/disks and stock kernel are retained. [Host prohibitions](host-boundary.md) remain in force.

## Current boot gates

| Candidate                                     | Verified                                                                                                                                                                              | Remaining gate                                                                                                                                                                               |
| --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Existing Android 16 SDK / 4 KiB kernel        | Diskless kernel boot/poweroff; separate SDK disks reach second-stage init, apexd, servicemanager and system_server attempts                                                           | No working SurfaceFlinger; Goldfish emulator HAL/topology mismatch, fresh filesystem feature mismatch                                                                                        |
| Latest checked SDK 37.2 / Android 17 / 16 KiB | Official repository metadata, bounded kernel/ramdisk download, kernel configuration inspection                                                                                        | No console output in bounded diskless probe; no successful boot claimed                                                                                                                      |
| Android 17 Cuttlefish / 4 KiB                 | Diskless memory pass; separate GPT/qcow disk reaches normal guest AVB/verity mounts, guest SELinux, APEX, BootControl, encrypted F2FS userdata, HWC, SurfaceFlinger and system_server | Boot completion and real boot-animation/lock-screen frame capture observed; integrated viewer/control transport is experimental, with severe 512 MiB pressure and launcher/SystemUI failures |

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

At the recorded 45-second sample, compositor services, SurfaceFlinger and a system_server process started, while missing virtual HAL services still prevented completing startup. The installed crosvm also reports that its native host display is unsupported. That initial service-startup checkpoint did not establish a GUI. Subsequent work captured real boot-animation/lock-screen frames and is integrated as the limited preview described below.

Providing a guest vsock device and the actual `vsock_lights_port` boot property let the existing guest Lights service get past its dependency; [its published implementation](https://android.googlesource.com/device/google/cuttlefish/+/a1162ca7a4e6297f1699b65052a8c2dd466fd518/guest/hals/light/lights.rs) starts a guest-side vsock server. It does not require changing the Galaxy's brightness service. The following gate was the absent virtual sensors service. In that 512 MiB sample, guest Linux then OOM-killed system_server at about 41 seconds. This is **guest RAM exhaustion**, separate from the hypervisor `NOMEM` failure in larger diskless memory probes. No Gunyah mapping fault was recorded in that Android sample. Raising RAM is not assumed to solve both problems.

## Next integration requirements

1. Prepare a separate, checked guest-only disk/ramdisk layout with matching fstab and AVB images. Preserve guest image verification and distinguish it from host Android AVB. Fresh userdata must use filesystem features supported by that guest's tools.
2. Verify first-stage mounts, matching guest modules, guest SELinux policy, SurfaceFlinger/composer, then actual frame output. An init/service start or VMM exit zero does not establish Android GUI support.
3. Connect an authenticated, bounded frame/input path to Bridge. Physical keyboard/mouse, focus release, rotation, resource limits and VM-local teardown need the same scrutiny as Debian's viewer.
4. Only after those tests expose a separate Android profile in the APK. Debian fallback must remain possible, and stopping it must never signal Android or an unrelated VM.

LineageOS/GSI userspace alone does not supply the matching vendor/HAL/device stack. The Android 17 Cuttlefish candidate moves the baseline forward without promising that arbitrary phone ROM images will boot.

## Integrated display preview (0.4.0-preview.4)

The app's **Settings → Android 17 guest** opens a separate experimental viewer.
The prepared profile runs the pinned Android 17 build with 512 MiB RAM, two vCPUs,
480×800 and software rendering. Boot animation and a real Android lock screen have
been captured on the Galaxy. This is not a claim of a stable, usable Android desktop:
512 MiB causes severe memory pressure, SystemUI/launcher ANRs and slow or failed
app launches. Home/Back/tap controls are provisional. The wake/unlock button wakes the guest and raises only its own display timeout to 30 minutes; it does not bypass a secure guest PIN. The input transport and this guest setting were verified; a reliably interactive launcher is not verified. Arbitrary LineageOS/GSI images
are not supported by this preset.

The viewer uses a root-peer-checked, app-UID-only local socket. Its native manager
connects only to the still-owned guest's fixed vsock CID/port, checks the pinned guest
identity, initializes guest zram and maps the three required virtual HAL console nodes.
The Cuttlefish sensor transport advertises an empty sensor set; it does not expose
Galaxy sensors. The guest OEM-lock transport reports unlocking denied and locked.
Neither implements host security, hardware attestation or changes host lock state.

The original **userdebug** image's adbd is used for guest-only setup and bounded PNG
capture. Its factory authentication configuration is not strengthened by this preview;
AUTH challenges are refused rather than bypassed. No guest NIC, host filesystem
share, physical USB device or listening host TCP server is added. This diagnostic guest
must not be treated as a production secure Android install. The existing guest image
verification checks are distinct from pvmfw/AVB attestation of the external ramdisk.

The viewer suspends image capture in the background. On closing it, the manager
requests sync and poweroff through the verified guest endpoint, waits for the tracked
child and falls back to signals for that child only. Unsaved guest work can be lost if
shutdown cannot complete. Session duration is capped at 30 minutes. Debian's
manager is stopped before opening this profile; its persistent disks are never attached.
The ordinary Debian profile remains selected and can be reopened afterwards.

### Prepare the fixed profile

OS images are **not bundled** in the APK. These are developer preparation steps for
the explicitly selected Galaxy, not a general importer or an automatic installer:

1. Fetch and verify the pinned files with `tools/fetch_android_guest.py --include-super`.
2. Build the regular-file-only filesystem helper from
   `docker build -t plb-fs-tools:trixie - < tools/android-fs-tools.Dockerfile`.
3. Run `tools/build_android_probe.py` once in a fresh output directory. It creates
   the separate base/working qcow2, misc and FRP virtual disks, original ramdisk and
   `android-lowram.cpio`. It refuses existing outputs. Preserve an existing working
   overlay; do not regenerate it over a used Android installation.
4. With the app installed and its owned VM stopped, place `android-base.qcow2`,
   `android-work.qcow2`, `android-misc.raw`, `android-frp.raw`, `android-ramdisk.cpio`
   and `android-lowram.cpio` in the app-private `files/android-guests/cuttlefish17/`.
   Place the extracted `Image` in `files/kernels/android17-cuttlefish/`.
   Root must own these regular files; directories must be 0700. Immutable base,
   kernel and ramdisks must be 0400; working overlay/misc/FRP must be 0600.
   Stage through private temporary files if normal `adb push` lacks access.
   Never copy to a block device, Android partition or another VM's directory.
5. The app verifies the exact kernel and low-RAM ramdisk digests before launch.
   The overlay's backing path is the fixed private Android base path produced by
   the builder. Open the Android entry in Settings and grant the app's existing
   root request if necessary. Vector needs no additional scope for this viewer.

Do not disable the guest Bluetooth package while its hardware feature remains
advertised: Android 17 treats that mismatch as a fatal configuration error on the
next framework start. An attempted memory reduction revealed this, and both guest
Bluetooth and NFC packages were re-enabled. No corresponding host package was changed.

### Diagnostics and limits

`adb -s "$GALAXY_ADB_SERIAL" logcat -s ProtectedLinuxAndroid` shows launcher/setup
and transport failures. Do not publish full device/guest logs. The app uses no host
UART log file; the bounded standalone diagnostic build keeps its guest console log.
The new parser tests exercise truncated/oversized packets, bad magic/checksum,
cancellation and the original concatenated ramdisk's preservation.

A separate 640 MiB Android probe reproduced the Gunyah address-map failure within
about ten seconds, before an interactive screen. Host boot ID remained unchanged,
SELinux stayed Enforcing, and the unrelated trusted VM was untouched. The release
therefore keeps 512 MiB and does not expose larger values as working. This limitation
is investigated in [the memory report](gunyah-memory-investigation.md).

## Separate GPU research sources

`tools/build_android_gpu_probe.py` builds independent GLES/Vulkan diagnostics,
with their own private profile, early guest console/bootstrap and a tightly
scoped process-local loader comparison. These research sources are retained
separately from the new memory probe. Both variants compile, but no additional
hardware-rendering success was established by the larger-memory tests above.
The successful 1536 MiB screenshots use the software/2D path. No loader shim or
experimental memory policy is enabled in the ordinary APK build.
