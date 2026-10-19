# Can Bridge hand preallocated RAM to the installed crosvm?

Static review, 2026-10-19. **A plain file-backed command-line mapping is not a
valid ordinary-memfd handoff through the reviewed installed backend.** The
viable design candidate is an explicit adoption path in the normal shared-RAM
constructor of a separately built, app-private userspace VMM. That path is not
implemented or boot-validated here, and does not create missing contiguous pages
or identify/repair an exhausted Gunyah allocator.

No device, VM, memory-pressure allocation, sysfs write or host reboot was used
in this review. The earlier single-trial permission remains consumed. The app's
guarded preview.7 runtime and its disk profiles are unchanged.

## Evidence from the actual saved VMM

The saved installed binary's SHA256 matches the previously pinned value:

```
9b4d56db70be1d3d646a08da0cb3e6eb95637457e5f15cb3b1c627f348e3e847
```

Its `.gnu_debugdata` contains compressed MiniDebugInfo. We extracted it on the
Mac and identified these actual function ranges:

| Function | Bytes in this image |
|---|---:|
| `hypervisor::gunyah::GunyahVm::new` | 1312 |
| `vm_memory::guest_memory::GuestMemory::new_with_options` | 1808 |
| `base::shm::SharedMemory` implementation of `PlatformSharedMemory::new` | 296 |

The retained function names, bounded text ranges and instruction inspection
provide evidence beyond comparing unrelated upstream versions. They do not
recover Samsung's complete source or prove runtime success. The actual ELF,
symbol tables and address-bearing disassembly stay in ignored private artifacts.
No modified analysis image was installed or executed.

### File-backed RAM takes the CMA route

The installed constructor distinguishes the file-backed option and sends that
path through ioctl **`0x40284115`**, corresponding to
`GH_VM_ANDROID_MAP_CMA_MEM`. Its ordinary RAM paths instead use:

- **`0x40204111`**: Android user-memory LEND.
- **`0x40204701`**: normal user-memory registration/SHARE.

The file-backed branch selects the CMA descriptor interface even when the
mapping is RAM. The saved help's `ram` option describes placement/type; it does
not promise to turn a generic file FD into a normal demand-paged-RAM registration.
This agrees with the corresponding
[DroidVM backend source](https://github.com/Droid-VM/crosvm/blob/849e05f12d5c8d4c36af8e15763986d242cd43b5/hypervisor/src/gunyah/mod.rs#L474),
but the conclusion here is based on the pinned installed image as well.

The already extracted active-slot host kernel contains `gunyah_cma_share_parcel`.
Its instructions obtain the FD's file, compare its file-operations pointer with
**`gunyah_cma_fops`**, and return **-22 / EINVAL** on the mismatched-FD path. A
standard shmem memfd does not have that dedicated driver's file operations.
The active-slot image is not a runtime-instruction read; no CMA ioctl, invalid
FD experiment, device-node open or allocation was performed to test this finding.

Thus a normal preallocated memfd cannot simply be supplied through this option
and treated as a dedicated CMA allocation. Renaming the file or using
`/proc/self/fd/N` does not change its kernel file operations. No other subsystem's
CMA pool is appropriated, and no CMA mapping/protection check is bypassed.

### The ordinary RAM constructor creates and seals its own memfd

The installed `PlatformSharedMemory::new` uses the AArch64 **memfd_create syscall
279** through `syscall`, then calls `ftruncate64`. Its observed flags are 3, or
11 in the conditional no-execute branch: close-on-exec/sealing flags plus the
additional execution policy. No external-RAM FD adoption was identified in this
normal constructor.

The installed `GuestMemory::new_with_options` then calls `fcntl` with command
**1033 / F_ADD_SEALS** and mask **7**, before constructing its memory-region
mappings. That is the grow/shrink/seal seal set, consistent with the reviewed
[shared-memory finalizer](https://github.com/Droid-VM/crosvm/blob/849e05f12d5c8d4c36af8e15763986d242cd43b5/vm_memory/src/guest_memory/sys/linux.rs#L28).
Any additional no-execute seal state must also be preserved; this static review
does not assert which conditional flag branch a future process will take.

This explains two unsuccessful shortcuts:

1. Passing an FD elsewhere does not stop this constructor from creating its own
   backing. The adoption point must be explicit.
2. A replacement object must satisfy the real sealing contract. If it already
   has F_SEAL_SEAL, an unconditional repeat F_ADD_SEALS can fail with EPERM; the
   [sealing API](https://man7.org/linux/man-pages/man2/F_ADD_SEALS.2const.html)
   does not permit pretending that failure succeeded.

The constructor's raw syscall also means hooking only a C `memfd_create` symbol
would miss this call. Interposing a variadic process-wide `syscall` function,
retargeting an FD after its mappings/aliases exist, or changing ioctl results
would require additional fragile assumptions. None was implemented.

## Candidate implementation boundary

The proposed change is confined to a **separate app-private crosvm userspace
build**, not the Android kernel, its APEX binary or any system partition. The
stock captured VMM and existing Microdroid/Debian modes remain available. A new
binary would require its own reviewed identity and pins, not relaxation of the
current exact-VMM guard.

1. **Prepare one owned original-style backing object.** Determine the entire
   shared backing size from the actual RAM layout. In the examined 1536 MiB
   configuration, 1504 MiB of main LEND RAM and 32 MiB SWIOTLB are distinct regions;
   a main mapping's length must not be assumed to equal its FD's total size. The
   source's original shared constructor sums non-file-backed regions. Preserve
   their FD offsets, ownership purposes and required boot margins.
2. **Prepare before Gunyah binds/donates it.** Verify useful actual folios without
   hole punching or changing an already donated mapping. Keeping the FD is not
   a guarantee against swap, splitting or migration. No new THP-policy permission
   or pressure-trial permission is implied by this design.
3. **Transfer the object, not copied bytes.** The producer closes its preparation
   mapping and hands an inherited/duplicated descriptor to the owned VMM through
   a fixed private mechanism. Object identity and exact size must survive. A
   content copy to a new memfd would allocate different physical backing.
4. **Adopt inside `GuestMemory`'s normal shared-object construction.** Validate
   FD identity, access, size, expected execution/seal state and single owner;
   create the same `SharedMemory`-backed regions. Keep `file_backed=None`, their
   actual purposes, protected main-memory LEND and ordinary SWIOTLB SHARE. A
   failed import must be visible; silently allocating fresh backing would hide
   failure and could violate the total-RAM budget.
5. **Handle seals explicitly.** Either let the importing constructor add the
   required seals before external aliasing, or accept an already sealed object
   only after verifying the complete required state. Reject incompatible write
   seals/access or policy. Do not fake success for a failed seal operation.
6. **Recheck donation assumptions.** The kernel's long-term pinning can have
   stricter requirements than an ordinary mapping or mlock. The
   [pinning documentation](https://docs.kernel.org/core-api/pin_user_pages.html)
   treats FOLL_LONGTERM as a more restrictive category. Folio observations before
   preparation do not prove the same physical layout at donation. Additional
   producer mappings/pins must not violate protected-lending rules.
7. **Preserve bounded cleanup.** Hold the existing owner lock for preparation and
   the owned VM lifetime, retain the 6 GiB total cap/headroom rules, stop only that
   VM, and release normal owned FDs after its cleanup. Never hole-punch a mapped
   folio, forcibly reclaim another VM's memory, clear taint or retry a failed
   protected mapping automatically.

This is a specific source-change design, not a new app feature or tested fork.
It avoids the demonstrated CMA misrouting, but **does not guarantee that the
initial useful folios can be obtained**, kept until donation, or mapped within
the installed EL2 resource budget. The last trial's 7.17% middle THP coverage is
still an independent obstacle. Neither freeing ordinary RAM nor delaying a
constructor manufactures the missing large folios.

## Reproduce static identification

The helper below verifies the exact saved-image digest, bounds ELF/XZ parsing,
extracts existing MiniDebugInfo and disassembles only the three identified ranges
from an exact non-executable snapshot of the verified bytes. It never launches
the saved ARM program and refuses a reused output directory. LLVM tools run on
the build machine; their invocation is not VMM execution.

```sh
python3 tools/inspect_crosvm_minidebug.py \
  --image artifacts/android-display/host-crosvm \
  --output artifacts/crosvm-backing-inspection \
  --llvm-directory /path/to/llvm/bin
```

Keep the generated ELF, full symbol list and assembly private. The JSON summary
lists function identities, sizes and hashes only; **instruction interpretation
still requires review**. Eight synthetic tests cover malformed ELF, excessive
decompression, incorrect pins, missing/ambiguous symbols and preservation of
existing files. No synthetic fixture is evidence of a runtime memory handoff.

[Sanitized findings](data/preallocated-ram-handoff-2026-10-19.json) ·
[Allocator investigation](gunyah-root-cause-review.md) ·
[Host boundary](host-boundary.md)
