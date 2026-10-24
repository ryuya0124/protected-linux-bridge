# Mounted-root binding audit

This developer diagnostic extends
[in-guest storage verification](native-ios-block-verification.md). It asks
whether the **actually mounted root** uses the exact live, cryptographically
verified storage provider. It does not enable owner-root acceptance, replace
Apple authentication, or continue to iOS user-space.

## Device result, 2026-10-24

The completed trial mounted the original main APFS root read-only and preserved
the real Apple root-auth errno **EAUTH (80)**. Its actual mounted BSD device was
**1:3**. Exactly one matching media had the pinned UUID and reached the exact
verified block provider in **9 nodes**. All seven audit conditions passed, and
the wrong-UUID, different-device and invalid-device queries all refused.
The verifier recorded 69 authenticated chunk loads / 330 page deliveries with
no failure or poisoned state. These include repeat reads, not unique coverage.
Owner acceptance remained **off**. The device was reset and the guest shut down
normally, with supervisor/VMM 0 and matching host postflight.

An earlier prepared build was not dispatched: the user normally rebooted Android
while Tail ADB was unavailable. The old snapshot and runner were retained, a
separate confirmed-current-boot snapshot was recorded, and a new nonce/kernel/
runner was built before deployment. No VM started during the disconnection.
The new boot's SELinux, six shmem policies, taint and four original disk metadata
identities matched the admission baseline. These disk checks are metadata, not
fresh full-disk hashes. No Android reboot was performed by this diagnostic.

The final full suite ran 307 tests: 304 passed and 3 skipped. The archive passed
the 190-slot original storage ABI audit and the guest source kernel linked.
The existing normal-data and tamper drivers' executable `__text` sections still
match the previously device-tested objects when the new option is absent.

## Checks performed

The guest first completes the same SHA-256 known-answer/table checks, verified
read path and real read-only APFS mount. After the actual Apple
`FSIOC_KERNEL_ROOTAUTH` call, it obtains the mounted filesystem's device vnode
using `vfs_devvp()`, reads its real `dev_t` using `vnode_specrdev()`, then releases
the vnode's acquired I/O reference.

The audit queries IOKit for that device's BSD major/minor pair. It requires:

- The actual filesystem is APFS and mounted read-only.
- The device is valid and has exactly one matching `IOMedia`.
- That media has the exact pinned main-root UUID.
- Its real provider chain reaches this exact `BridgeOriginalPciBlock` instance.
- That block instance has a qualified hash table, remains online, has verified
  data deliveries and a valid private snapshot, and has no failure or poisoned
  state. The UUID must also match the image identity pinned by this provider.

Provider traversal is limited to 16 nodes. Global matching iteration has a
128-object budget; incomplete or duplicate matches refuse qualification. The
audit uses actual object identity rather than accepting a provider merely
because its name resembles the expected class.

Three local negative queries must refuse: an altered UUID, a different valid
BSD device number, and `NODEV`. They do not modify the registry, mounted
filesystem, actual UUID, verification state, shared DMA, or persistent disk.

The audit records each check independently, all three negative refusals, and a
separate `root_binding_audit` result whose acceptance field remains **0**.
Apple's real errno is recorded before these checks. The diagnostic then takes
the block device offline, resets it and shuts down its own guest normally.
This observation hook cannot authorize a root or reach user-space.

## Build and evidence separation

Add `--root-binding-audit` to the explicit main-root mount/verification build:

```sh
python3 tools/build_original_xnu_block_probe.py \
    --source artifacts/ios-native-research/unixnu \
    --audit /private/path/to/qualified-original-storage-audit.json \
    --candidate /private/path/to/qualified-prelinked-kernel \
    --root-disk artifacts/ios-native-research/apple-main-raw-1 \
    --block-policy artifacts/ios-native-research/main-root-block-policy-1 \
    --root-binding-audit --mount-probe --nonce NEW_32_HEX_NONCE \
    --output artifacts/ios-native-research/new-root-binding-probe
```

This only compiles files. The builder rejects a missing verification policy or
combining the audit with a corrupted-data control. The generated trial contracts
must explicitly record `root_binding_audit: true`, retain
`owner_root_policy_enabled: false`, and use a new boot-bound one-use runner.
`XNU_OWNER_ROOT_AUDIT` and `XNU_OWNER_ROOT_TRUST` remain 0: the first-party
observation does not select the upstream generic native-virtio/root-trust path.

`native_xnu_binding_result.py` requires the exact ordered binding evidence
between the real root-auth receipt and device teardown. It then checks the
complete existing storage/crypto/mount/shutdown evidence. The plain read-only
verifier refuses an audit-marked profile; it cannot silently qualify a missing
binding measurement. Mixed nonces, missing/duplicate receipts, failed negative
controls and an enabled acceptance field all refuse qualification.

Host ASan/UBSan tests exercise the same provider traversal and audit predicates
for cycles, alien owners, depth exhaustion, missing/duplicate media, writable or
non-APFS roots, invalid identity and unavailable verification state. Evidence
tests preserve the actual Apple refusal and refuse incomplete or promoted claims.

## Remaining work

This is a point-in-time binding audit. A future explicit owner-root policy must
use the qualified decision at the actual authorization boundary and retain
verified fail-closed storage for every later read. It must keep Apple's refusal
distinct from the owner's decision. This diagnostic does not implement that
acceptance mode, launchd, writable Data, SystemOS/cryptex deployment, process
signing, platform services, graphics or input.
