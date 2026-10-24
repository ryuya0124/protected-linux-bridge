# Native iOS-family guest: module-free XNU bring-up

2026-10-24. The user prefers a recent iOS and explicitly rejects CPU emulation.
The target remains execution on the Galaxy, with the host Android preserved.
The Galaxy has now executed native protected UEFI and a source-built XNU
kernel. This is **early kernel bring-up**, not an iOS home screen, a working
iPadOS installation or an enabled app profile. No Apple OS rootfs was fetched.

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

## Module-free path and local results

Bridge's already inspected crosvm source distinguishes executable private guest
RAM/BIOS regions from a non-executable static SWIOTLB shared region. A candidate
port would keep UEFI/XNU executable code in private guest RAM and use a bounded,
non-executable shared DMA window only for I/O. This contract has now been
exercised by the local UEFI probes and the early
XNU serial boot described below. It does not establish a complete iOS I/O stack.
The current protected VM and its memory ownership rules remain intact.

Simply changing the VMM flag is insufficient. At the inspected build-driver pin,
[NativeVirtioBlock](https://github.com/UniXNU/NativeVirtioBlock/blob/98909eb9198a4360d7a43be831887cb8b74d814d/NativeVirtioBlock.cpp)
allocates its virtqueue and transfer storage from general physically contiguous
kernel memory and submits their physical addresses to the device. In a private
protected guest this does not establish host readability. The inspected firmware
already supplies a shared-DMA bounce-buffer allocator
through GunyahIoMmuDxe; a port still needs the equivalent in the guest XNU drivers,
plus a matching reserved device-tree window and checked buffer bounds. Display
and input have equivalent ownership requirements. A comment or ADT entry alone
cannot move allocations into that window.

Two separate 512 MiB / one-vCPU UEFI trials passed on SM-S948Q, Android 17
S948QOPU1BZI9. They used the stock, hash-pinned APEX crosvm, protected private
executable RAM and its 32 MiB non-executable restricted DMA pool. The only block
device was a new, read-only 2 MiB nonce-pattern disk. Every byte was read and
checked, followed by guest PSCI shutdown and VMM exit 0. A closed-channel
VcpuControl warning during teardown remains in the raw record.

The second trial also supplied the generated FDT and guest register receipts:
RAM `0x80000000+0x20000000`, shared DMA `0x9e000000+0x02000000`, GIC distributor
`0x3fff0000`, redistributor `0x3ffd0000`, virtual byte-wide 16550 UART `0x3f8`,
19.2 MHz architectural counter and boot CPU affinity 0. These are this VM's
virtual devices, not physical Galaxy device addresses. The second FDT contains
fresh seeds, so the raw binary and trial identities stay private.

The first XNU serial trial loaded the newly built DarwinBoot EFI driver and a
source-built Darwin 23.6.0 / XNU 10063.141.1 kernel. It reached `kprintf initialized`,
printed its Darwin version, completed VM bootstrap and then panicked at
`kalloc.c:1779`, `n_zones_sig + n_zones_type + 1 <= 32`. The supervisor stopped
only its owned VMM at its 45-second diagnostic deadline (exit 124). A guest
panic/banner is not an iOS boot or successful normal guest shutdown.

The XNU ADT gives DarwinBoot a 256 MiB private allocation and excludes the
32 MiB shared DMA window from general/executable RAM. No original guest disk,
GPU, network or forwarded physical device is attached. First-party guest polling
UART code supplies diagnostics without peeking into protected guest RAM.
The second XNU trial used the upstream board's supported
`kt_fixed_zones=120` budget and passed that point. It published the CHRP NVRAM
proxy, registered CPU 0 and logged all 19 corecrypto POST cases passed, including
integrity. It later panicked because the pthread function table was absent.
No allocator assertion or crypto selftest was removed. Both failures remain
recorded separately; neither is a root-filesystem or iOS boot. A third trial
linked the real matching source pthread component, passed BSD network
initialization/selftests and reached waiting for Apple_HFS root media. It ended
with runtime safety stop 88, whose exact instantaneous cause was not captured;
postflight matching does not turn that stop into a successful trial. The runner
now records guard categories. Later fresh PCI trials retained the same strict
guard; they do not reinterpret the earlier unexplained stop as harmless.

The later module-free XNU PCI test read and verified every byte of a separate
2 MiB synthetic disk, then powered off the guest and returned VMM/supervisor 0.
The firmware excludes a 64 KiB tail from its shared allocator, and XNU uses only
an exclusive 8 KiB portion for queues and data. The first PCI attempt's guest
unmap panic is preserved; corrected page-rounded unmapping passed. This proves
the narrow PCI/shared-DMA transport, not IOMedia, filesystem mount or iOS boot.

An additional source-only build compiled 14 public IOStorageFamily units and
linked their genuine source archive into the same XNU kernel build. Its four
kernel-local helper imports required no stub or export change. That larger
kernel has not been device-booted or registered as a working storage service.

A subsequent kernel **has** exercised the source storage family: its PCI-backed
IOStorage adapter published a real read-only IOMedia and BSD node, verified all
2 MiB through IOMedia plus a cross-page read, rejected a write without issuing
device I/O, reset and powered off normally. This is a synthetic-disk service
test; no filesystem mount, Apple rootfs or iOS user-space has been booted.

Postflight verified unchanged host boot and core/trusted-VM process births,
taint 4608, Enforcing SELinux, all six shmem settings still `never`, and
unchanged device/inode/size/mtime/ctime of the three original Android files and
Debian persistent disk. Disk metadata checks are not claimed as fresh full-disk
content hashes. No host module, compaction, pressure helper, policy write,
physical GUI input or Android restart was used.

## Remaining work before an iOS guest

1. Preserve the locally verified private-executable/static-shared-DMA contract.
   Do not reintroduce executable runtime host-share parcels or host modules.
2. Continue the separately pinned UEFI/XNU runtime and matching device tree.
   Preserve the current Android 17 and Debian runtimes and disks. Start with
   kernel serial output and a new guest-only disk, without GPU/network/hardware
   forwarding or attachment of existing guest disks.
3. Apply existing ownership, healthy-host and RAM admission checks. The reference
   5 GiB guest is not evidence that this Galaxy can supply that backing. Keep the
   6 GiB diagnostic cap and 1536 MiB host floor; no new compaction is approved.
4. Extend the tested synthetic IOMedia service to a separately verified root-disk
   profile and filesystem stack; the 2 MiB service is not a mounted filesystem.
   Provide display/input transport suitable for that memory contract,
   rather than reading protected host mappings. Reuse guest-only networking
   principles; do not copy upstream host interface or bridge changes.
5. Verify serial boot, guest shutdown and unchanged host state before display.
   Only then qualify a new release. Treat iOS/iPadOS 18/26/27 as separate ports,
   not firmware selectors for the 17.7.11 prelink configuration.

The reference launch remains incompatible with the host constraints, but the
module-free native UEFI and early XNU path is no longer just a hypothesis.
The app still has no iOS launch action: the remaining storage, user-space,
graphics and input requirements are substantial.

The kernel builds use Apple's open-source XNU tag, a separately pinned
third-party corecrypto source snapshot and upstream ArmDTPlatform/ArmCpu.
Provider compilation, namespace checks and POST HMAC population alone do not
prove runtime crypto selftests; the second local XNU trial subsequently logged
the actual POST results. This is not a certification claim. The first generic bootstrap link failed on a disabled
legacy-display probe referenced unconditionally; the checked-in guard fixes that
build configuration without adding a fake graphics service.

Only source/build tools and small new diagnostic disks were obtained. No full
IPSW, proprietary iOS kext collection, Apple root filesystem or complete OS
installation was downloaded. Sources/build caches are ignored and space-bound;
Android 16 frozen refs and the shipped Android/Debian runtime remain unchanged.
See the [reproduction and evidence notes](native-xnu-bringup.md) and the
[structured record](data/ios-native-feasibility-2026-10-24.json).

Later work fetched only the official Apple manifest/kernelcache ranges (about
18 MB of a 6.8 GB restore archive), verified the original decoded kernel pin,
and emitted a separate APFS dependency candidate. No OS/SystemOS root image or
full IPSW was obtained. Its original corecrypto passed 19 POST cases on the
Galaxy, followed by full synthetic PCI readback and normal guest shutdown.
APFS mount and iOS user-space remain unverified; the early diagnostic produced
no APFS-specific execution receipt. Static original storage ABI checks passed,
but its concrete PCI adapter still needs guest-side qualification.

## Other assessed approaches

- [darwin-vm](https://github.com/jprx/darwin-vm) uses a QEMU fork and documents
  shell-only boot rather than a usable iOS GUI. CPU emulation is excluded here.
- [qemu-ios-android](https://github.com/KD3533/qemu-ios-android) reports old iOS
  screens on Android, but is excluded by both the version preference and CPU
  emulation constraint.
- [vphone-cli](https://github.com/Lakr233/vphone-cli) uses Apple's Mac-only
  Virtualization.framework/PCC path. It is not a Galaxy-local implementation;
  a remote Mac viewer would be a different product and has not been selected.
