# Native iOS-family guest: source assessment

2026-10-24. The user prefers a recent iOS and explicitly rejects CPU emulation.
The target remains execution on the Galaxy, with the host Android preserved.
No iOS guest has been built, installed or booted. This is an offline assessment,
not an enabled app profile or a working iOS release.

## Execution model

A Linux container shares the Android/Linux kernel and cannot boot an XNU
kernel. An iOS-family guest needs a separate XNU kernel plus supported platform
devices. The candidate here executes ARM64 code through Gunyah hardware
virtualization. Paravirtual devices and guest driver adaptations are still
needed; avoiding CPU instruction translation does not remove device modelling.

The promising upstream is [UniXNU-droidvm](https://github.com/Droid-VM/UniXNU-droidvm),
which reports a home screen on Snapdragon SM8650/SM8850. That is a third-party
report, not verification on this Galaxy. The reference configuration is
**iPadOS 17.7.11 / 21H461 / iPad7,1**, not current iOS and not an iPhone firmware.
Newer firmware cannot be substituted without matching kernel/prelink work.

The boot sequence is an app-private VM FAT payload containing UniXNU.efi,
DarwinBoot.efi and patched guest XNU, started by edk2-gunyah. It is not an
Android boot-partition image. The reference board describes a 5120 MiB / four-vCPU
layout and MMIO devices for block, input, GPU and networking. These addresses
and its Apple-format device tree must be regenerated together for a new layout.

## Audited pins and decisive code

- Device repository: [`512350a37a80f6b368e889aee22fafbcf79a28a4`](https://github.com/Droid-VM/UniXNU-droidvm/tree/512350a37a80f6b368e889aee22fafbcf79a28a4).
- UniXNU build driver: [`1a01c2afa197d975d04da892325c56b6b14aaecb`](https://github.com/UniXNU/UniXNU/tree/1a01c2afa197d975d04da892325c56b6b14aaecb).
  Upstream's device instructions mention its `droidvm` branch; the inspected
  main-branch driver has not been qualified as a matching build set. A lookup
  of `refs/heads/droidvm` returned no branch at assessment time.
- crosvm, wip/virtio-mmio: [`61c47a4693bc2e514c5be3f4cb1b9d727727528f`](https://github.com/Droid-VM/crosvm/tree/61c47a4693bc2e514c5be3f4cb1b9d727727528f).

[`board.toml`](https://github.com/Droid-VM/UniXNU-droidvm/blob/512350a37a80f6b368e889aee22fafbcf79a28a4/board.toml)
pins the firmware and prelink hashes. The inspected GPU configuration has
changed beyond the README's CPU-only/missing-GPU status. Documentation differs;
no full GPU qualification is inferred.

[`load-modules.sh`](https://github.com/Droid-VM/UniXNU-droidvm/blob/512350a37a80f6b368e889aee22fafbcf79a28a4/tools/rig/load-modules.sh)
loads host modules, including gunyah-host-share. It must not run on this device.
In crosvm's
[`hypervisor/src/gunyah/mod.rs`](https://github.com/Droid-VM/crosvm/blob/61c47a4693bc2e514c5be3f4cb1b9d727727528f/hypervisor/src/gunyah/mod.rs),
`start()` calls `share_guest_ram_window()` after VM start. In the sharing mode,
`share_blob()` opens `/dev/gunyah_share` for runtime memory parcels, including
execution permission for the guest RAM window. This is a substantive host-driver
contract, not a missing file that may be created or a launch flag that may be
ignored. The current Bridge protected-memory path is not a drop-in replacement.

The reference rig also uses a gh_hugepage_reserve pool, host Wi-Fi bridge
addresses and live shared-memory inspection. None is authorized for adoption.
Do not read a donated/protected mapping using those framebuffer/debug tools.
The repository records host watchdog failures; changing the ADT or omitting a
host module does not establish a safe fallback.

## A concrete module-free research direction

Bridge's already inspected crosvm source distinguishes executable private guest
RAM/BIOS regions from a non-executable static SWIOTLB shared region. A candidate
port would keep UEFI/XNU executable code in private guest RAM and use a bounded,
non-executable shared DMA window only for I/O. This is a source-derived design
hypothesis, not evidence of successful XNU execution or compatibility with the
installed host. The current protected VM and its memory ownership rules must
remain intact.

Simply changing the VMM flag is insufficient. At the inspected build-driver pin,
[NativeVirtioBlock](https://github.com/UniXNU/NativeVirtioBlock/blob/98909eb9198a4360d7a43be831887cb8b74d814d/NativeVirtioBlock.cpp)
allocates its virtqueue and transfer storage from general physically contiguous
kernel memory and submits their physical addresses to the device. In a private
protected guest this does not establish host readability. A port needs an actual
shared-DMA allocator/bounce-buffer path in guest UEFI and the guest XNU drivers,
plus a matching reserved device-tree window and checked buffer bounds. Display
and input have equivalent ownership requirements. A comment or ADT entry alone
cannot move allocations into that window.

This provides a specific adaptation target before considering a large iPadOS
build. Do not copy upstream's live-memory readers to work around it. First
establish a separately built, small native UEFI serial/DMA probe through allowed
interfaces, with no original data disk attached. Its firmware build and device
execution have **not** been performed in this assessment.

## Required adaptation before any Galaxy boot

1. Establish a guest boot-memory contract using the existing permitted host
   interfaces. Remove dependency on executable runtime host-share parcels;
   do not replace it by loading a module or altering Gunyah protection.
2. Build a separately pinned UEFI/XNU runtime and its matching device tree.
   Preserve the current Android 17 and Debian runtimes and disks. Start with
   kernel serial output and a new guest-only disk, without GPU/network/hardware
   forwarding or attachment of existing guest disks.
3. Apply existing ownership, healthy-host and RAM admission checks. The reference
   5 GiB guest is not evidence that this Galaxy can supply that backing. Keep the
   6 GiB diagnostic cap and 1536 MiB host floor; no new compaction is approved.
4. Provide storage/display/input transport suitable for that memory contract,
   rather than reading protected host mappings. Reuse guest-only networking
   principles; do not copy upstream host interface or bridge changes.
5. Verify serial boot, guest shutdown and unchanged host state before display.
   Only then qualify a new release. Treat iOS/iPadOS 18/26/27 as separate ports,
   not firmware selectors for the 17.7.11 prelink configuration.

The first step is unresolved. The upstream launch is therefore **blocked under
the current host constraints**. This assessment does not request lifting them.
No placeholder launch button, emulator fallback or iOS success claim was added.

Only about 8 MiB of research sources were retrieved; no Apple firmware,
submodule/source tree build, host security adjustment or device connection was
performed. Full firmware extraction/build is deferred because the Mac has
limited space. Pinned findings are also in the
[structured record](data/ios-native-feasibility-2026-10-24.json).

## Other assessed approaches

- [darwin-vm](https://github.com/jprx/darwin-vm) uses a QEMU fork and documents
  shell-only boot rather than a usable iOS GUI. CPU emulation is excluded here.
- [qemu-ios-android](https://github.com/KD3533/qemu-ios-android) reports old iOS
  screens on Android, but is excluded by both the version preference and CPU
  emulation constraint.
- [vphone-cli](https://github.com/Lakr233/vphone-cli) uses Apple's Mac-only
  Virtualization.framework/PCC path. It is not a Galaxy-local implementation;
  a remote Mac viewer would be a different product and has not been selected.
