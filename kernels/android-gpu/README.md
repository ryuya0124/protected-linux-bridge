# Android guest DMA synchronization experiment

This builds **a separate ARM64 VM guest Image**. It does not build, install,
patch or flash the Android host kernel. The normal guest Image, Debian disks,
Microdroid mode and installed crosvm remain available.

The pinned input is Android common commit
`3ec022196c4e9d5c1434599cdda63f622dd6f586` (6.12.74), matching the original
Android 17 guest generation. `guest-bound-dma-sync.patch` backports upstream
[598c1c3e895590f845e04455d5580ea28ffde666](https://github.com/torvalds/linux/commit/598c1c3e895590f845e04455d5580ea28ffde666).
It primes partially written DMA backing, waits for conflicting transfers and
performs the CPU synchronization at completion before signaling fences.
The existing 6.12 DMA-API condition replaces a newer upstream helper; this
is not a host Gunyah or memory-protection patch.

The fragment retains Android F2FS, device-mapper encryption/verity, Binder and
SELinux. Required virtual-device drivers are built in and guest module loading
is disabled. This avoids mixing a custom kernel with the original guest's
ABI-specific modules. Nested KVM and VM-manager interfaces are disabled inside
this guest; the outer protected Gunyah VM is unchanged. Compiler AutoFDO is disabled because its
matching profiling input is not part of this reproducible source archive.

## Build without touching a device

Use an ARM64 Docker builder, the exact compressed source archive and the saved
original guest configuration. Both input hashes are checked by the tool.
The source archive is available from
[AOSP's GitHub mirror](https://github.com/aosp-mirror/kernel_common/archive/3ec022196c4e9d5c1434599cdda63f622dd6f586.tar.gz).
The original configuration is extracted from the pinned guest Image and stays
in ignored artifacts with that image.

```sh
docker build -t plb-android-gpu-kernel:clang19 kernels/android-gpu
python3 tools/build_android_guest_kernel.py \
    --source-archive artifacts/android4g-gpu/guest-kernel-build/android-common-3ec022.tar.gz \
    --base-config artifacts/android-display/cuttlefish17/kernel.config \
    --output artifacts/android4g-gpu/guest-kernel-build
```

The container has no network and writes to its own case-sensitive Linux volume;
its recipe and pinned inputs are read-only. This matters because files such as
`xt_hl.c` and `xt_HL.c` cannot coexist on a default case-insensitive Mac checkout.
It uses four CPUs and at most 4 GiB on the Mac. No device is mounted.
Outputs include the Image, final `.config`, System.map, build log,
release string and a JSON provenance manifest. Debian package updates can
change the builder, so the resolved Docker image identity is recorded rather
than claiming bit-for-bit reproducibility across arbitrary future builders.

Monitor free Mac storage. After successful compilation and evidence capture,
retain the outputs, configuration, manifest/logs and exact compressed source
archive. Only the verified source extraction and this build's intermediate
objects may be removed; do not prune unrelated Docker data. On success the tool
exports the completed outputs and removes only its own stopped build container
and volume. On failure it retains that invocation for diagnosis; its identities
are recorded in `builder-instance.json`.

## Separate developer profile

`tools/build_android4g_probe.py` accepts paired `--guest-kernel` and
`--guest-kernel-manifest` arguments. It checks the Image, configuration and
patch identities, the exact release string and the required built-in drivers.
It creates different fixed manager/proxy/policy binaries and a ramdisk override
with an empty guest module list. Original init, security properties, module
binaries and AVB data remain byte-identical in the retained archive.

The native copy supervisor's fixed `--custom-guest` option creates
`cf17-gpu4096-dma-sync` from verified offline copies, refusing an existing
destination. The stock `cf17-gpu4096-adopted` profile is not replaced. The
manager pins the new Image/ramdisk separately. The existing common owner lock,
healthy-host checks, six-key 40-second restoration guardian, 6 GiB combined
budget and bounded VM observation still apply.

This is developer tooling, not a released APK option. A build, host renderer
initialization or transfer return code alone cannot establish Android GPU
success. See [the measured trials](../../docs/android4g-gpu-investigation.md).

The driver patch and Linux-derived build work follow GPL-2.0-only. First-party
Python helpers retain the repository's MIT license. Kernel binaries and source
archives are not committed or redistributed by this recipe.
