# Native main OS root storage checkpoint

2026-10-24. The Galaxy has mounted the original iPadOS main restore volume
through the protected native ARM64 guest. The mount is read-only. Apple's
subsequent root authentication returns **EAUTH (80)**. The diagnostic records
that result and shuts down its own guest; iOS user-space and its UI do not run.
No iOS launch button, installer or new APK is enabled by this result.

The follow-up [guest storage verification](native-ios-block-verification.md)
adds real per-read SHA-256 checks and a separately observed tamper refusal.
Apple root-auth still returns 80; no owner-root acceptance is enabled.

This extends [native XNU bring-up](native-xnu-bringup.md). It retains the
qualified 512 MiB / one-vCPU VM, stock hash-pinned crosvm, private executable RAM
and separate non-executable 32 MiB DMA pool. There are no host modules, shared
executable mappings, Android reboot, host tuning writes or original-disk attachments.

## Exact images and observed operations

The compatibility anchor is **iPad7,1 / j120ap / iPadOS 17.7.11 / 21H461**,
not current iOS. The full IPSW was not fetched or published. Only selected Apple
CDN members were obtained using version-bound range requests.

| Input | Size in bytes | SHA-256 |
| --- | ---: | --- |
| Main OS DMG, `044-76040-038.dmg` | 5,157,406,069 | `3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d` |
| Independently converted GPT/raw image | 6,077,580,800 | `e5634d04a8ac35f024845f583394e0a35deb26a04cb0c00286a8674e25a9a4f2` |

Both source-manifest SHA-384 digests and the selected-member ZIP CRC matched.
The raw image was hashed on the Mac and again on Android before starting the VM.
No extra multi-GiB Mac copy is needed: builders reference the prepared raw image.
On Android the separate diagnostic input is root-owned, mode 0600, single-link
and attached using `ro=true`. Original Android/Debian disks stay detached.

The live guest chain is:

```text
BridgeOriginalPciBlock
  IOBlockStorageDriver
    IOMedia (disk0)
      IOGUIDPartitionScheme
        IOMedia (disk0s1)
          AppleAPFSContainerScheme
            AppleAPFSMedia (disk1)
              AppleAPFSContainer
                AppleAPFSVolume (disk1s1)
```

The actual volume is `DawnUpdate21H461.J120J121J207J208OS`, UUID
`04769D26-2CA0-4825-B443-E2E5F88300F2`. The driver searches only its own
subtree, requires exactly one UUID match and sets standard `/chosen/root-matching`.
It does not guess a BSD device name or select an unrelated system volume.

Real BSD reads verified the GPT prefix (4,096 bytes) and the final partial page
(1,536 bytes), with their independently computed FNV values. **5,632 guest bytes
were compared**, not the whole six-GB disk. APFS separately read its real metadata,
recognized checkpoint xid 418 and completed `vfs_mountroot()` with return 0.
The mount flags confirm read-only APFS. The 16-bit queue wrap remains covered
offline; this trial is not a hardware wrap stress test.

## Three explicitly dispatched trials

| Trial | Observation | Supervisor / guest shutdown |
| --- | --- | --- |
| Main root 1 | UEFI loader returned `EFI_DEVICE_ERROR` before XNU entry | 0; firmware requested shutdown. No kernel/root boot. |
| Main root 2 | Bounded loader read the complete kernel and ADT; BSD prefix/tail passed. Root search stopped at its old depth-8 limit. | 0; explicit diagnostic failure and guest shutdown. No mount. |
| Main root 3 | Depth-16 / 128-node search selected the real volume at depth 8; VFS mount 0, read-only APFS, actual root-auth errno 80 | 0; reset and normal guest shutdown. No iOS user-space. |

Each trial has a new nonce, separately built archive/kernel/FAT/runner, a durable
one-use receipt, preflight and postflight. Trials are manually dispatched; there
is no VM retry loop. The exact read-only main image was moved between consumed
trial directories after VMM exit and rehashed by each fresh runner. Failed
results and spent receipts remain preserved.

The guest loader patch [0006](../tools/native_xnu_patches/0006-darwinboot-bounded-file-read.patch)
uses [BoundedFileRead.h](../tools/native_uefi_probe/BoundedFileRead.h) to request at
most 1 MiB per EFI file read, advance by actual successful progress and reject
zero/oversized results. Errors report the filename and completed byte count.
The FAT label/serial were also aligned with the qualified FAT preparation during
trial 2; the comparison does not prove which detail caused the first failure.
ASan/UBSan tests cover short reads, EOF, invalid counts, error propagation and
progress. The patched guest-only DarwinBoot driver was rebuilt against the
archived pinned EDK2 source; BaseTools' 304 tests passed after removing archived
AppleDouble metadata files. The unrelated emulator's absolute X11 symlink was
not restored. No emulator was run.

## Mounting is separate from authentication

The main restore volume has no snapshots in read-only Mac inspection, yet this
original APFS implementation actually mounts it. The earlier AppCryptex panic
about a sealed live root therefore must not be generalized to the main image.
Do not fabricate a snapshot name or disable a sealing check to explain it away.

[Original Apple XNU](https://github.com/apple-oss-distributions/xnu/blob/d8b80295118ef25ac3a784134bcf95cd8e88109f/bsd/kern/bsd_init.c)
requires `VNOP_IOCTL(rootvnode, FSIOC_KERNEL_ROOTAUTH, ...)` after mounting a
non-macOS root. [Its errno definition](https://github.com/apple-oss-distributions/xnu/blob/d8b80295118ef25ac3a784134bcf95cd8e88109f/bsd/sys/errno.h)
defines 80 as `EAUTH`. The observation hook runs **after that real operation**;
it does not replace the errno with success. The guest records 80 and shuts down
before the ordinary failure panic or any user-space continuation.

The console also reports missing `system-volume-auth-blob` and a required root
hash. These are observations, not proof that adding one property would fix the
entire boot chain. The pinned `SystemVolume` member is a 229-byte IM4P `isys`
payload requiring personalization in both restore identities. No personalized
IM4M has been obtained or qualified. Matching manifest digests and successful
corecrypto POST do not authenticate the boot root. Apple's
[SSV description](https://developer.apple.com/news/?id=3xpv8r2m) likewise treats
volume integrity and the signature check as part of a boot chain, not just a
successful filesystem mount; that document describes macOS, not this iPad guest.

Next work must distinguish a correctly deployed Apple-authenticated root from
an explicitly different owner-verified guest policy. A future owner policy needs
real in-guest cryptographic checks of consumed storage data and negative tamper
controls, with its trust anchor and limitations documented. A prelaunch host
hash or the two FNV comparisons is insufficient for that claim. Neither
`XNU_OWNER_ROOT_TRUST` nor an authentication-success stub is enabled here.
The remaining SystemOS/cryptex, writable Data volume, process launch, platform
services, display and input also need qualification; a root-auth change alone
does not establish a usable iOS installation.

## Reproduction boundary

These are explicit preparation commands only; none deploys or starts a VM:

```sh
python3 tools/build_original_xnu_block_probe.py \
    --source artifacts/ios-native-research/unixnu \
    --output artifacts/ios-native-research/new-original-main-probe \
    --audit artifacts/ios-native-research/pinned-original-storage-abi.private.json \
    --candidate artifacts/ios-native-research/exact-audited-candidate \
    --nonce FRESH_32_HEX_DIGITS \
    --root-disk artifacts/ios-native-research/apple-main-raw-1 \
    --mount-probe

python3 tools/build_native_uefi_runner.py \
    --output artifacts/ios-native-research/new-main-trial \
    --host-snapshot artifacts/ios-native-research/fresh-host.private.json \
    --sdk /path/to/Android/sdk \
    --xnu-serial --pci-probe \
    --root-disk artifacts/ios-native-research/apple-main-raw-1
```

Build the matching source guest kernel with the archive and diagnostic patch
0005, as described in the bring-up notes; retain the real crypto POST sealing.
Patch 0006 targets the separately pinned DarwinBoot repository, not XNU.
Copy the identical helper into its `DarwinBootPkg/Driver` directory and build
that driver against the pinned EDK2/UniXNU protocol headers. Include it with the
fresh kernel/ADT in a new 64 MiB FAT16, checked using `fsck.fat -n`.
Populate the per-trial manifest from actual payload hashes. Old spent trial
directories, generated headers, nonces or host-process identities must not be
overwritten to requalify another run.

The runner still requires the exact healthy host, unchanged core/trusted-VM
births, all six shmem policies `never`, 2,560 MiB initial MemAvailable, owner and
maintenance locks, and its 45-second diagnostic supervisor. It rechecks health
and admission after the large input hash. No memory floor or guard was lowered.
All three postflights preserved host boot/process identities, taint 4608,
Enforcing, the six policies and four original disk metadata identities. Those
disk metadata checks are not fresh whole-disk hashes.

Final validation ran 297 project tests: 291 passed and six environment-dependent
tests skipped. The independent BaseTools suite passed 304 tests. All three
postflight comparisons and the strict third-trial evidence parser passed.
The APK and frozen Android 16 refs are unchanged. Experimental inputs can be
removed only after checking the matching spent nonce and absence of an owned
VMM; preserve result/log/spent receipts and the verified Mac source images.
