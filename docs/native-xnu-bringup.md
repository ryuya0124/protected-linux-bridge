# Native protected XNU diagnostics

These developer tools are not an iOS installer. They run on the Android 17
branch and leave the released Android/Debian profiles unchanged. The target is
native ARM64 execution through the existing Gunyah/crosvm interface. No QEMU CPU
translation, remote Apple machine or host kernel module is used.

## Evidence, 2026-10-24

| Trial | Actual observation | Limit |
| --- | --- | --- |
| UEFI 1 | EL1 entry, 32 MiB shared DMA pool, exact full 2 MiB pattern read, guest shutdown, VMM 0 | Not XNU or an OS |
| UEFI 2 | Same complete read/shutdown, plus generated FDT and CPU register metadata | Raw FDT includes random seeds and stays private |
| XNU 1 | Darwin 23.6.0 banner and VM bootstrap | Guest allocator assertion, owned diagnostic stopped at 45 s, result 124 |
| XNU 2 | Allocator passed with upstream `kt_fixed_zones=120`; NVRAM, CPU 0, all 19 corecrypto POST cases passed | Missing pthread registration panic; result 124 |
| XNU 3 | Real source pthread linked; BSD network initialization/selftests and waiting for Apple_HFS root media | Runtime safety stop 88, exact instantaneous cause unresolved; no successful boot/shutdown claim |
| XNU PCI 1 | Both payload/transport preparation passed; first PCI block device detected | Guest panic from an incorrectly unaligned/permanent-map unmap; supervisor 124 |
| XNU PCI 2 | Both PCI block devices detected; all 2,097,152 synthetic bytes checked through the exclusive shared DMA tail; guest PSCI shutdown and VMM/supervisor 0 | A transport test, not IOMedia, filesystem or iOS user-space boot |
| XNU media 1 | Real IOStorage/IOMedia/BSD node, full 2 MiB read, cross-page read and write refusal; guest shutdown and supervisor/VMM 0 | An initial dispatch refused before VM start at 79; no filesystem or iOS user-space boot |
| Original dependency 1 | Original Apple corecrypto passed 19 POST cases; full synthetic PCI readback, reset, guest shutdown and VMM/supervisor 0 with an APFS dependency candidate | No APFS mount or APFS-specific execution receipt; the early transport probe exits before root selection |

The generic XNU bootstrap initially failed to link because a disabled legacy
video probe was referenced unconditionally. The source guard retains the real
enabled-feature call. Later builds linked the real platform, crypto and pthread
components; no function-table or crypto-success stub was added.

The third trial's safety result is preserved even though postflight identities,
health, policies and disk metadata matched. The original runner did not record
which of identity/read-health, memory-floor or kernel-monitor checks stopped it;
postflight cannot reconstruct that instant. A later read-only kernel-log
follow-up contained the already documented normal `svprt` BUG-prefixed printks;
this does not identify the exact consumed monitor record. One intervening Tail
ADB root request was denied, then a subsequent root-verified read succeeded;
that transient failure is retained and not presented as a host crash.
The source now logs the guard
category. Later PCI trials retained the strict monitor and separate fresh
receipts; they did not bypass or reinterpret this unresolved earlier stop.

A diagnostic deadline, safety stop or guest panic is not successful shutdown. In particular,
`MACH Reboot` in an XNU UART trace describes a **guest** panic path; it is not an
Android restart command. Each completed trial separately checked the host boot
identity, core and trusted-VM process births, taint, SELinux, six shmem policies
and original disk metadata. Full original-disk hashes were not recomputed.

## Sources and build inputs

All third-party sources, generated files and OS images stay in ignored
`artifacts/ios-native-research/`. First-party diagnostics are MIT; dependency
licenses remain their own. No Apple OS image or firmware is redistributed.

| Component | Reviewed revision |
| --- | --- |
| edk2-gunyah | `90c4d52c7d11485318bbb4fb9291a15ea1550ab2` |
| Its EDK2 submodule | `b7a715f7c03c45c6b4575bf88596bfd79658b8ce` |
| UniXNU build scripts | `1a01c2afa197d975d04da892325c56b6b14aaecb` |
| Device ADT generator | `512350a37a80f6b368e889aee22fafbcf79a28a4` |
| Apple XNU 10063.141.1 | `d8b80295118ef25ac3a784134bcf95cd8e88109f` |
| ArmCpu | `1951cc8444b340d7d8879e9418220cf29eb711d7` |
| ArmDTPlatform | `0d544705c2e89725caabc0542bb96bc8febd8c70` |
| DarwinBoot | `d099354d0eab34df5088ca1ba89c9c88bc3260bb` |
| NativePrelink | `2b6ffb50117eee820b98aca9bd51b5c7dfb2970e` |
| corecrypto-2023 snapshot | `11fba65b12d8830630e248dfa8344caf8b065f26` |
| Apple libpthread 519.120.4 | `c0e60fb5ecfc54cf379c261d3f9572cc22ded891` |
| Public IOStorageFamily, compile/link audit | `e45fbe30a7879e0b16e26dddb78538f1b00e0bff` |

The corecrypto snapshot comes from
[ONLYA/corecrypto-2023](https://github.com/ONLYA/corecrypto-2023), not an
independently authenticated Apple archive. Logs of its software POST tests are
not a module certification or proof of a complete iOS security architecture.

UEFI was compiled in native ARM64 Linux with GCC and EDK2 BaseTools. XNU was
compiled natively on the development Mac with Xcode 27.1, for ordinary ARM64,
iOS 17 deployment and 4 KiB guest pages. The diagnostic host runner uses Android
NDK 28.2.13676358. A native ARM64 Docker build is not target CPU emulation.

## UEFI probe preparation

Use a fresh source directory below this repository's ignored `artifacts/`.
Checkout the exact firmware revision, initialize its recorded EDK2 submodule,
and initialize EDK2's **first-level** dependency pins. Avoid recursive OpenSSL
test/development checkouts. Build in one consistent workspace so submodule Git
paths remain valid. Keep the build network disabled after fetching dependencies.

```sh
python3 tools/prepare_native_uefi_probe.py \
    --source artifacts/ios-native-research/edk2-gunyah \
    --output artifacts/ios-native-research/new-uefi-trial
```

This command never connects to a device or starts a VM. It refuses unreviewed
source/output paths and existing result directories. It creates a nonce-pattern
2 MiB disk and injects the guest-only UART/readback probe into the boot manager.
The current probe also reports CNTFRQ and MPIDR. Build the firmware with
`./build.sh -DPCI_CAM_MODE=TRUE -n 2` in the audited native Linux toolchain, then
copy `edk2-gunyah.fd` to the new trial's `firmware.fd`.

```sh
python3 tools/build_native_uefi_runner.py \
    --output artifacts/ios-native-research/new-uefi-trial \
    --host-snapshot artifacts/ios-native-research/fresh-host.private.json \
    --sdk /path/to/Android/sdk
```

The private snapshot supplies the current host boot UUID and exactly three
`cores` entries for zygote64, system_server and the trusted Samsung crosvm:
`name`, `pid`, and `/proc/PID/stat` field 22 as `start_ticks`. The builder compiles
but does not deploy or launch. Never reuse an old boot's identities or copy a
spent trial's nonce to obtain another run.

Deployment is deliberately manual. Stage only the new firmware, synthetic disk
and runner, verify their hashes, then copy to the exact newly generated
`device_directory` under app-private storage as root-owned mode 0600 (runner
0700). Run that runner with `--run` once. Do not replace this with a direct VMM
invocation or an upstream rig script. Preserve its `spent` receipt and collect
`console.log`, `vmm.log`, `guest-generated.dtb` and the supervisor output before
removing only that trial's shell staging files. After these completed trials,
their consumed device-side firmware/disks/runners and shell duplicates were
removed, while app-private spent receipts and logs were preserved.

`native_uefi_result.py` checks the full ordered, nonce-bound EL/DMA/readback/
shutdown record and VMM exit separately. `ios_boot_verified` remains false.

## XNU serial port

The first patch in `tools/native_xnu_patches/` applies to the pinned **UniXNU
build-script tree**. The second is a patch for the **XNU source worktree** and
can be placed as `patches/xnu/bridge-ns16550-console.patch` in the private
UniXNU tree so its existing baseline build applies it. Do not apply either to
an Android host kernel. `BridgeUart16550.inc` is the readable source of that
second patch's polling UART addition.

Bootstrap the existing upstream 4 KiB build with `XNU_BOOTSTRAP=1`. Use its
actual compiler records to build ArmDTPlatform and ArmCpu with the reviewed
device repository's `build-arm-driver.py`. Build the pinned corecrypto provider
plain and namespaced; link only the namespaced version. Build matching
libpthread with `build-libpthread-kernel.py`. Then build with:

- `XNU_BOOTSTRAP=0`, `XNU_SOURCE_CORECRYPTO14=1`, `XNU_SOURCE_PTHREAD=1`.
- `XNU_ARM_DT_PLATFORM=1`, `XNU_ARMCPU=1`, `XNU_DT_PANIC_REGION=1`.
- `XNU_BOOT_PROGRESS=0`, two build jobs, and `-DXNU_BRIDGE_UART16550=1` added to
  `XNU_EXTRA_CFLAGS` **alongside** the baseline warning flags.

Keep the baseline's real POST HMAC sealing step. Build DarwinBoot's driver INF
against the same pinned EDK2 plus the UniXNU protocol package; no graphical menu
is required for the serial-only diagnostic. The guest firmware uses
`BootXnu.c` in place of the readback probe, a fresh nonce header and the unchanged
reviewed `UniXNULoader.h` interface. Add SimpleFileSystem to that boot-manager
INF's protocol dependencies. Each compiled firmware remains separately pinned.

```sh
python3 tools/prepare_native_xnu_adt.py \
    --device-source artifacts/ios-native-research/device \
    --probe artifacts/ios-native-research/completed-uefi-trial \
    --output artifacts/ios-native-research/new-xnu-trial/XNU.ADT
```

This generator validates the upstream source pin, successful UEFI evidence,
nonce-bound CPU metadata, RAM/DMA boundary and byte-wide virtual UART. It gives
XNU 256 MiB private owned RAM, excludes the top 32 MiB shared DMA region, and
adds a root-level absolute-address UART node. It does not invent a physical
Galaxy peripheral or use a protected-memory reader. Its receipt is preparation,
not evidence of device boot.

Create a **new** 64 MiB FAT16 disk with dosfstools/mtools, containing `XNU`,
`XNU.ADT`, and `DARWIN.EFI`. Check it with `fsck.fat -n`. Attach it read-only;
never attach an existing Android/Debian disk. Record SHA-256 for every component
and the resulting disk in that trial's private `probe.json`. The existing runner
builder's explicit `--xnu-serial` selects this separate profile and its disk
shape. It still does not launch or deploy anything. The common supervisor retains
exact host/VMM pins, boot/process births, owner/maintenance leases, health/RAM
checks, durable one-use receipts and a 45-second diagnostic-only deadline.

`native_xnu_result.py` records kernel execution, panic and supervisor status
separately. A version banner, normal VMM exit, missing root, timeout or crypto
selftest log never becomes `ios_boot_verified=true`.

## Guest PCI/static-DMA readback

`build_native_xnu_probe.py` compiles `BridgePciBlock.c` with the recorded XNU C
ABI and creates a new 2 MiB nonce-pattern disk. `prepare_native_xnu_adt.py
--pci-probe` validates the successful UEFI FDT's exact CAM topology, bus 0 and
PCI apertures before adding the diagnostic node. CAM is not ECAM: the mapped
first bus uses 256 bytes per function, at `0x10000+0x10000`.

Apply patch `0003-reserve-guest-dma-tail.patch` only to the pinned **guest EDK2
GunyahIoMmuDxe** source. It excludes the final 64 KiB from the firmware's
32 MiB bounce allocator. XNU uses 8 KiB at `0x9fff0000` within that exclusive
tail for split-queue descriptors, request/status and a 4 KiB data buffer. General
and executable XNU RAM still excludes the shared pool. No host page permission,
kernel, module or policy is changed.

Build this distinct kernel with its generated archive force-loaded through
`XNU_EXTRA_LD_ARCHIVES`; compile guest `BootXnu.c` with
`PLB_XNU_PCI_PROBE=1`. Prepare a new 64 MiB FAT payload containing that kernel,
ADT and DarwinBoot, plus the separate 2 MiB synthetic disk. Use the runner's
explicit `--xnu-serial --pci-probe`. Never attach an existing guest disk.

The driver accepts exactly the two expected capacities, negotiates a read-only
modern VirtIO PCI queue, checks every byte of the synthetic disk and resets the
device before guest-only PSCI shutdown. There is no write API. Capability/BAR
bounds, private-DMA refusal, malformed completions, timeouts and ring wrap are
exercised offline with ASan/UBSan. Later parser hardening also rejects a 64-bit
BAR's upper half and prevents queue reuse after an invalid completion. That
hardening was recompiled and tested offline, not rerun on the device.

PCI trial 1 incorrectly paired `ml_io_map` with a 56-byte `ml_io_unmap`, causing
a guest pmap panic. Trial 2 uses `ml_io_map_unmappable(..., VM_WIMG_IO)` and
page-rounded kernel-map unmapping. It read all 2 MiB, checked the nonce-derived
pattern/FNV64, shut down its guest and returned supervisor/VMM 0. Original disks
remained detached; postflight boot/core/trusted-VM births, taint 4608, Enforcing,
six `never` policies and original disk metadata matched.

`native_xnu_pci_result.py` requires the complete ordered receipts, exact byte
count/digest, shared address, fresh runner identity and absence of a kernel
panic. Reused firmware may carry a separate `firmware_nonce`; its successful
old runner cannot authorize a new trial. `ios_boot_verified` remains false.

## Public storage source build

`audit_native_xnu_storage.py` compiles unmodified pinned IOStorageFamily sources
against the actual XNU `IOService` compiler record. It generates the genuine
DriverKit header/implementation from the public `.iig`, rather than substituting
a class stub. The initial audit compiled 8/10 units: generated DriverKit headers
were missing and C++23 rejected legacy `register` spelling. A second audit
compiled 13/14 after generation, but found an incorrect generated-header include
layout. The corrected audit compiled all 14 units. Only `-Wno-register` changes
the spelling diagnostic; third-party storage sources remain unmodified.

The 443 distinct object imports resolved against the source kernel plus the
compiled objects. Four helpers are defined inside the kernel but not exported
to independent kexts: three UBSan overflow handlers and `__zone_site_register`.
No alias/stub/export patch was added. Force-loading the genuine source archive
during the existing full XNU source link succeeded, including real POST HMAC
population. That linked kernel has **not** been booted. Compile/link success does
not establish class registration, ABI qualification, IOMedia publication or mount.

Both tools are compile/audit-only; they never contact ADB or launch a VM.

## Genuine media connection

The subsequent **media diagnostic kernel** adds a real `IOStorage` subclass and
the source family's `IOMedia`/`IOMediaBSDClient`. It runs from the actual
`IOKitBSDInit` point after `publishResource("IOBSD")`, guarded by the guest-only
compile switch and diagnostic boot argument. Patch 0004 targets guest XNU,
not Android/Xposed; disassembly confirms the call. The diagnostics share guest
mapping support. The media build must link `libBridgePciMedia.a`, never the raw
probe's early-shutdown archive.

The adapter serializes its queue, bounces through the same exclusive 8 KiB
shared window and copies into private memory descriptors. It publishes only the
separate read-only 2 MiB fixture. Device writes/unmap are not implemented.
A reset failure retains mappings until guest teardown.

The first dispatch ended 79 before a spent receipt or VMM existed. A nearby
snapshot had about 2.24 GiB available, below the unchanged 2.5 GiB start floor.
Six freshly checked cached third-party processes accepted supported BACKGROUND
trim notifications; Chrome was skipped after its state changed. No app was
killed; no cache data, compaction, tuning or policy was changed. A later snapshot
was about 2.86 GiB; causation by trim is not established. A separate manual
dispatch, after confirming no prior VM receipt, passed.

Actual receipts prove source-family registration, read-only media publication,
a real BSD name, full 2 MiB content/FNV64 verification through IOMedia and a
4096-byte read at offset 512 crossing two DMA pages. The source iOS IOMedia's
LockedWrite error rejected a write; device requests stayed 514 before/after.
Device reset, guest PSCI shutdown and VMM/supervisor 0 followed. Host postflight
matched. This newer media kernel was booted; the earlier standalone source-link
kernel was not. Neither mounts a filesystem.

`build_native_xnu_media_probe.py` is compile-only with source/archive pin checks.
`native_xnu_media_result.py` rejects partial reads, writable media, absent BSD
nodes, private DMA, issued writes, stale runner IDs, panic and partial shutdown.
The runner now also names each admission-79 category; that logging-only change
was compiled after this trial, not substituted into its pinned runner.

## Original Apple dependency route

`inspect_native_ios_payload.py` reads the pinned Apple CDN archive using exact
HTTP206 byte ranges, matching Content-Range and ETag. Its total fetch budget is
128 MiB; a server's whole-file response is rejected before body reading. It
validates the board/build identity, bounds ZIP members and extracts only the
manifest/optional kernelcache. IM4P parsing and the LZFSE child are bounded;
decoded ARM64 kernelcache must match the board's exact SHA-256. It never flashes
a device, downloads the complete IPSW or starts a VM. Use `--decode-kernelcache`
to qualify the decoded kernel, rather than treating ZIP CRC as a board pin.

The actual reference archive is 6,835,124,840 bytes. Reading the manifest and
kernelcache fetched 17,790,160 range bytes. The decoded 46,415,872-byte kernel
matches `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`.
No OS/SystemOS root image was fetched. This is the upstream iPad7,1 / iPadOS
17.7.11 compatibility anchor, not a claim to support the newest iOS.

The pinned upstream NativePrelink planner/emitter resolved APFS's 12 declared
components, including original IOStorageFamily/corecrypto/CoreTrust/AppleImage4.
It rebound 17,883 kernel and 7,920 component pointer sites while preserving
original executable bytes. The candidate remains experimental: static
resolution is not runtime qualification or a complete instruction/ABI proof.
The actual original storage ABI audit also passed: 190 slots, 176 kernel
bindings, 14 abstract slots, public class size 0x90, real protected constructor
forwarding to IOService and trapping abstract complete destructors. Its live
metaclass check remains unperformed; host guest-RAM peek tools are forbidden.

For the separate original-dependency diagnostic, build with
`XNU_CRYPTO_BOOTSTRAP=prelinked`: the source provider's startup entry is absent,
so the original corecrypto starts normally rather than registering twice.
Keep the source-family archive out of this variant; including it together with
the original family would duplicate classes. The source-family media mode above
remains independently preserved. Prelink the raw-probe source kernel with the
original APFS closure at the reviewed -0x1000000 slide; do not run rig scripts.

This separate 512 MiB test actually booted, logged the original corecrypto's
19 POST passes including integrity and successful initialization, read all
2 MiB through static shared DMA and shut down with VMM/supervisor0. Postflight
preserved boot/core/trustedVM births, taint4608, Enforcing, six never policies and
four original disk metadata identities. No APFS-specific console receipt or
filesystem mount occurred: presence in a prelink image is not proof a filesystem
was initialized. Original storage imports must next be bound to a concrete PCI
device and checked inside the guest before a general root-disk profile.

The user subsequently allowed memory release in response to closing heavy apps.
Six narrowly targeted cached user-0 apps received ActivityManager `am kill`
requests after fresh state checks. Foreground/process-protected targets stayed
excluded; no force-stop, kill-all, task/data deletion, tuning or compaction was
used. This is later than the trim-only media trial above. Request acceptance and
memory snapshots are not proof of exact causal reclaimed bytes.

## Remaining I/O contract

UEFI's PCI VirtIO disk uses `GunyahIoMmuDxe` to bounce I/O through the static
shared pool. That does not automatically fix XNU. The reviewed NativeVirtioBlock
uses MMIO and general contiguous allocations; the current stock crosvm disk is
PCI and private guest buffers are not host-readable. The new PCI readback proves
the transport and exclusive shared bounce-buffer path for a tiny synthetic
read-only disk. The later media trial connects it to genuine storage services
and verifies one cross-page read. A general root-disk profile, broader
sizes/concurrency and filesystem support remain before an iOS rootfs is attached.
Display, input and networking need the same ownership
review. The reference executable-share module and RAM-peeking GPU path remain
excluded.

Keep raw logs, boot IDs, nonces, seeds, compiled kernels, firmware and signing
material private. Preserve negative trials as well as progress. Newer iOS-family
firmware requires separate version/ABI work; the upstream 17.7.11 prelink recipe
is not compatibility proof for iOS 18/26/27.

Mac disk pressure interrupted one corrected kernel link before VM deployment.
Owned firmware sources/logs were archived with CRC/hash verification. At the
user's request, inactive Xcode simulators and regenerable caches were removed,
reclaiming about 28 GiB; Xcode, SDKs, projects and archives were retained. The
8 GiB build-space guard stayed enabled and the resumed link passed.

## Original Apple storage: live BSD path

The next protected512MiB/one-vCPU trial uses `BridgeOriginalPciBlock.cpp`,
compiled from public block-device declarations and bound to real original
IOStorageFamily functions/data. `build_original_xnu_block_probe.py` verifies
190 virtual slots,14 concrete abstract operations, the exact candidate and
header/source identities, and rejects unaudited imports or abstract complete
destructors. Ten genuine original bindings were used; no function body was
replaced with a success stub. The earlier source-family variant is preserved.

The guest's normal metaclass API returned144bytes, matching the compiled base.
Original storage matching created a BSD node. The diagnostic opened its actual
character-device entry, read every2MiB via `uio`/`d_read`, verified the expected
FNV64, and closed it. Device reset, guest PSCI shutdown and VMM/supervisor0
passed. `native_xnu_original_result.py` verifies ordered nonce-bound receipts,
class size, private/shared DMA boundary, full digest, crypto POST and shutdown.
APFS2236.140.6 also logged actual module start and sysctl registration.
**No filesystem was mounted and no iOS user-space was booted.**

The host retained the same boot/core/trustedVM births, taint4608, Enforcing,
six `never` settings and four original disk metadata identities. No fresh
original-disk content hashes are claimed. No host tuning/restart/module change.

A separate small official `Cryptex1,AppOS` image was range-fetched for the next
filesystem step:10MiB raw APFS, SHA-256
`52e568e1eaaede5fde4684ad10afc60f8fb236bfb5a23e0869bb0ed8a1d275d7`.
Its extraction consumed4700776range bytes, with a32MiB member cap and the
unchanged128MiB range budget. The main OS/SystemOS images and full IPSW were
not downloaded. The small AppOS component is not a complete bootable iOS root
and has not been attached to a VM at this checkpoint.
