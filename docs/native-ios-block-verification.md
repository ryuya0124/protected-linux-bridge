# Native guest storage verification

2026-10-24. This developer-only checkpoint verifies consumed main-root data
inside the native ARM64 guest. It extends the
[read-only APFS mount checkpoint](native-ios-root-storage.md). It does not enable
an iOS installer, an owner-root acceptance policy, or a new APK launch mode.

## Trust anchor and read path

The input remains the independently converted, read-only iPadOS 17.7.11 / 21H461
main OS image: 6,077,580,800 bytes, SHA-256
`e5634d04a8ac35f024845f583394e0a35deb26a04cb0c00286a8674e25a9a4f2`.
The owner builds a fixed table from this exact image, then links it into the
private guest kernel. This is an **owner-defined input policy**, not Apple
signing, personalization, IM4M, or a replacement result from Apple's root-auth
operation.

Each table row is:

```text
SHA256("PLB-IOS-BLOCK-v1\0" || LE64(chunk_index) || LE32(actual_length) || data)
```

Chunks are 16 KiB, except the final partial chunk. Index and actual length bind
the digest to its position and geometry. The table has 370,947 entries and is
11,870,304 bytes. Its fixed SHA-256 is
`a62091ae6738616977611e632597bd9a2adfee0a57708a3f264dd9ac158e4395`.
The builder pins both this table hash and the complete image hash in source;
editing a manifest and recomputing its hash cannot select a different image.

Before publishing storage, the guest calls the real XNU SHA-256 implementation
for the `abc` known-answer test and the complete embedded table. The real Apple
corecrypto POST remains a separate observation. A per-device lock serializes
the following path:

1. Read the required 16 KiB chunk through the bounded PCI/shared-DMA transport.
2. Keep the completed bytes in a separately allocated guest-private snapshot.
3. Hash that private snapshot and compare every digest byte with the pinned row.
4. Copy only verified requested bytes to the I/O descriptor used by the original
   storage stack and APFS. Pad beyond the physical image's partial final page
   with zeroes; never authenticate or publish untrusted transport padding.

Only the most recent verified chunk is cached, in private guest memory. A later
DMA change cannot replace those cached bytes. A transport/hash failure clears
the private snapshot, marks the path poisoned and takes the device offline.
Further requests cannot fall back to the old unverified reader or a previously
valid cached page. No test changes the actual disk image.

The 16 KiB table granularity keeps the existing 64 MiB FAT and 512 MiB guest
profile. It may read additional bytes around a requested page; this is not a
performance claim. The prelinked table's Mach-O segment declares read-only,
non-executable permissions; that static inspection is **not** a hardware
measurement of the guest's actual page-table permissions.

## Runtime evidence

The normal-data trial passed its in-guest SHA-256 known-answer and complete-table
checks. It authenticated **69 chunk loads** and **330 page deliveries**, mounted
the exact main APFS root read-only, and observed the real Apple root-auth errno
**EAUTH (80)** before resetting the device and shutting down the guest normally.
These counters include repeated reads; they are **not unique whole-disk
coverage**. Host hashes independently covered the complete source image.

Neither `XNU_OWNER_ROOT_AUDIT` nor `XNU_OWNER_ROOT_TRUST` was enabled. The guest
does not continue to launchd or other iOS user-space after the observation.

A separately built and explicitly dispatched negative control flipped one byte
in the first completed private read, at offset 0. The guest detected it before
delivering any verified chunk or page: counters were **0 / 0**, with one failure
and a poisoned path. BSD subsequently reported the unavailable device; there
was no successful prefix/tail comparison, root selection, mount or root-auth
call. The guest reset the device and shut down normally. Both supervisors and
VMMs exited 0; the negative control's intended refusal is not a successful mount.

Before these two trials, the user confirmed intentionally soft-rebooting Android
since the previous checkpoint. The old host snapshot was retained, and a
separate current-generation snapshot was recorded before building fresh runners.
Neither trial rebooted Android. Both postflights matched that new boot/core/
trusted-VM identity, taint, SELinux state, six unchanged shmem policies and four
original disk metadata identities. These original-disk checks are metadata
comparisons, not fresh whole-disk hashes. The original persistent disks stayed
detached; only one diagnostic main-image copy was moved between the completed
trials and independently rehashed by the new runner.

The complete offline suite ran 304 tests: 298 passed and 6 were skipped. Both
real guest archives compiled against the checked 190-slot storage ABI, and both
source kernels linked. Formatting was followed by another complete suite and
recompilation; each driver's entire executable `__text` section matched its
respective device-tested object. This comparison does not claim a separate
hardware trial of reformatted debug metadata.

## Reproducing preparation

These commands prepare files only. They do not connect, change host settings,
or launch a VM. Use the exact source and dependency revisions documented in
[native XNU bring-up](native-xnu-bringup.md), fresh output directories, and a
new nonce for each explicitly dispatched trial.

```sh
python3 tools/build_native_ios_block_policy.py \
    --root-disk artifacts/ios-native-research/apple-main-raw-1 \
    --output artifacts/ios-native-research/new-main-block-policy

python3 tools/build_original_xnu_block_probe.py \
    --source artifacts/ios-native-research/unixnu \
    --audit /private/path/to/qualified-original-storage-audit.json \
    --candidate /private/path/to/qualified-prelinked-kernel \
    --root-disk artifacts/ios-native-research/apple-main-raw-1 \
    --block-policy artifacts/ios-native-research/new-main-block-policy \
    --mount-probe --nonce NEW_32_HEX_NONCE \
    --output artifacts/ios-native-research/new-verified-root-probe
```

`--inject-block-fault` prepares a distinct negative-control build. It flips one
byte only in the first completed guest-private read, leaving the source image,
shared DMA and host unchanged. Never reuse an already dispatched nonce or call
the VMM directly to retry a failed trial. The normal profile without
`--block-policy` remains available; the verification option is restricted to
the exact main-root mount diagnostic.

The host C fixture uses CommonCrypto on macOS or OpenSSL on Linux to exercise
the **same** read-verification and hash headers under ASan/UBSan. Tests cover
independent Python/C digests, cache isolation, tampering, table-index substitution,
each transport-read failure, malformed geometry, and partial final pages.
The separate result parser requires nonce-bound cryptographic, storage, mount,
authentication and shutdown evidence; VMM exit 0 alone is insufficient.

## Remaining boundary

The follow-up [mounted-root binding audit](native-ios-root-binding.md) now
qualifies the actual device, UUID, exact verified provider and negative queries
on the Galaxy. Its acceptance field is still 0.

A future explicit owner-root acceptance mode must bind this verified provider
to the actual mounted root device and UUID, prove that the path remains online
and unpoisoned, retain the real Apple refusal, and pass negative binding controls.
This checkpoint does not make that trust decision. SystemOS/cryptex deployment,
writable Data, process signing, platform services, graphics and input also remain
unqualified. A verified APFS read path is not a completed iOS boot.
