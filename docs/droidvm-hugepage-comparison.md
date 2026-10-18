# DroidVM HugePage design compared with Bridge

Status: source review and read-only Galaxy measurements, 2026-10-19. **No host
setting, kernel/module, SELinux policy, partition, protection mechanism or reboot
operation was used. No larger VM was launched for this review.** The user permits
Android's ordinary reclamation of other apps while working toward a 4 GiB guest;
that does not authorize global THP changes or kernel modifications. The released
APK remains unchanged. The earlier [runtime-policy proposal](memory-policy-proposal.md)
is suspended under the current instructions, not approved for execution.

The [subsequent measurements](memory-provisioning-followup.md) include a successful
4 GiB host allocation, resident-target collapse counters, file-seal checks and an
actual generated guest DTB. This page records the preceding source comparison.

## Conclusion

DroidVM's approach is relevant: actually supplying larger backing folios can
reduce Gunyah's mapping and tracking work. It is more than accepting crosvm's
`--hugepages` flag. Its current implementation combines host policy changes,
preparation in a modified VMM, and a kernel-managed reserve. **Those components
have not been demonstrated to solve this Galaxy's failure, and the kernel/policy
parts are outside the permitted implementation boundary.**

Bridge has observed hypervisor `addrspace_map` returning `NOMEM`, after a backing
folio has already been obtained. That is evidence against treating every such
failure as an ordinary host RAM allocation failure. The exact exhausted internal
resource remains unproved. A folio-size experiment would be discriminating only
if the larger folios are independently verified before donation.

## Revisions reviewed

The private source cache is about a few MiB, not a full Android/kernel checkout.
No third-party executable or module was installed, built or run.

| Project | Pinned revision | Relevant entry points |
|---|---|---|
| DroidVM | `33488301c902124d9ddb09fa04b8126443d83230` | `VMBackendInstance.cleanUpMemory`, `CrosvmBackendInstance`, `PoolPreflight`, `LendMthpMode` |
| gh-hugepage-reserve | `e15c40d600dd19b2c5bcb220a92f7359c9ee0846` | `parts/gh_hooks.c.inc`, pool/kapi code, `service.sh` |
| DroidVM crosvm fork | `849e05f12d5c8d4c36af8e15763986d242cd43b5` | `gunyah/mod.rs`, `mthp.rs`, `pin.rs` |
| Public Gunyah hypervisor | `254727746fce809aba0e629a13711a954b246968` | address-space hypercall, sparse mapping/range map and page-table allocators |
| Bridge | Base `5abb0f8`; this review adds the read-only audit | `native/console_bridge.c`, `native/android_guest_manager.c`, diagnostic runners |

These upstream revisions are source references, **not a proven match to Samsung's
installed kernel/hypervisor or even to every shipped DroidVM binary**. The actual
Galaxy's BTF verifies error enum names/values, not allocator implementation bodies.

## End-to-end comparison

| Layer | DroidVM reviewed source | Bridge released path / measured probes |
|---|---|---|
| Host THP policy | Sets anonymous THP and defrag to `madvise`, shmem THP to `advise` | No writes; Galaxy anonymous `always`, shmem `never`, defrag `never` |
| Other host preparation | Global cache drop and compaction; fork preparation also enables intermediate anonymous THP sizes | No global cache drop/compaction; previous cache release was limited to owned immutable files |
| VMM request | `--hugepages`; configured `--prepare-lend-mthp-mode single/chunked` | Normal managers do not request `--hugepages`; diskless diagnostic has explicitly compared it on/off |
| Backing | Shared-memory backing with large-page preparation and optional reserve supply | Observed `crosvm_guest` memfd; anonymous backing is a separate diagnostic experiment |
| Physical-page guarantee | Module can substitute reserved order-9 pages for eligible VM allocations | No reserve module, no proven large-folio supply |
| LEND preparation | Populate/collapse/pin, then optionally divide into at most 256 MiB regions | Installed stock crosvm does not advertise the fork's preparation option; its plain huge-page advice did not fix the measured failure |

The app's [`cleanUpMemory`](https://github.com/Droid-VM/DroidVM/blob/33488301c902124d9ddb09fa04b8126443d83230/app/src/main/java/cn/classfun/droidvm/daemon/vm/VMBackendInstance.java)
is called during launch, and performs **host-wide writes**. Its
[command builder](https://github.com/Droid-VM/DroidVM/blob/33488301c902124d9ddb09fa04b8126443d83230/app/src/main/java/cn/classfun/droidvm/daemon/vm/backend/CrosvmBackendInstance.java)
selects the flags above. Copying its command line or running its preparation
routine is therefore not a permitted isolated experiment here.

The fork's [`prepare_lend_region`](https://github.com/Droid-VM/crosvm/blob/849e05f12d5c8d4c36af8e15763986d242cd43b5/hypervisor/src/gunyah/mthp.rs)
combines process-local operations with global writes. It populates in 64 MiB
batches, requests collapse, attempts locking, and verifies population. Its
256 MiB LEND split addresses a potentially large kernel page-pointer array;
**chunking does not itself turn base pages into huge folios or repair hypervisor
mapping exhaustion**. Bridge's recorded failure occurs during guest execution,
not a demonstrated startup `kcalloc` failure.

## What the reserve actually supplies

At a 4 KiB host page size, order 9 is 2 MiB. The module's
[allocation hook](https://github.com/Droid-VM/gh-hugepage-reserve/blob/e15c40d600dd19b2c5bcb220a92f7359c9ee0846/parts/gh_hooks.c.inc)
filters the requested order, nonempty reserve, recognized VM owner and movable
allocation flag. It replaces the allocator's result with an owned reserve page,
returning an unused ordinary result if necessary. Separate hooks track VM
creation/destruction, process exit and returned pages. This is active modification
of host memory management, not an ordinary application allocating a big buffer.

The reserve's [design and implementation](https://github.com/Droid-VM/gh-hugepage-reserve/blob/e15c40d600dd19b2c5bcb220a92f7359c9ee0846/POOL_DESIGN.md)
maintain contiguous pages and their custody across use and return; refill can
involve migration, reclamation and CMA-related handling. Do not equate those
operations with access to an existing free CMA pool. Bridge does not appropriate
Android/GPU/subsystem CMA memory.

Its [boot service](https://github.com/Droid-VM/gh-hugepage-reserve/blob/e15c40d600dd19b2c5bcb220a92f7359c9ee0846/package/module/service.sh)
sets shmem THP to `advise`, so an advised shmem fault can request the allocation
that the module intercepts. **Advice selects an allocation attempt; the reserve
supplies pages when ordinary allocation cannot.** Merely changing the advice
without a usable physical supply would not reproduce the full design.

## Why folio size can matter to Gunyah

The related public Linux
[`gunyah_gup_demand_page` / `gunyah_vm_provide_folio`](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/blob/8e66bb68fe6735e93ef5dd59c184cf6341b0098d/drivers/virt/gunyah/vm_mgr_mem.c)
path pins a userspace page, obtains `page_folio(page)`, then uses `folio_size` for
donation and address-space mapping. Although the pin request is for one page,
that source recovers the containing folio; **a one-page GUP request alone is not
proof that a real THP is ignored**. Kernel-version differences must be checked.
The observed map-error string follows successful donation in this reference.

In an ideal fully aligned 4 GiB RAM region, 4 KiB backing means 1,048,576 folios;
2 MiB backing means 2,048. That is a 512-fold reduction in folio-level operations,
not a measured 512-fold decrease in every internal table, allocation or fault.
Device/shared regions and alignment can change the real result.

The public hypervisor's
[`hypercall_addrspace_map`](https://github.com/quic/gunyah-hypervisor/blob/254727746fce809aba0e629a13711a954b246968/hyp/mem/addrspace/src/hypercalls.c)
selects partial mapping. The
[sparse mapping implementation](https://github.com/quic/gunyah-hypervisor/blob/254727746fce809aba0e629a13711a954b246968/hyp/mem/memextent_sparse/src/memextent_sparse.c)
first inserts a range record and then maps the memory. Both the
[range-map implementation](https://github.com/quic/gunyah-hypervisor/blob/254727746fce809aba0e629a13711a954b246968/hyp/misc/range_map_gpt/src/gpt.c)
and [page-table implementation](https://github.com/quic/gunyah-hypervisor/blob/254727746fce809aba0e629a13711a954b246968/hyp/mem/pgtable/armv8/src/pgtable.c)
allocate hypervisor partition memory. The outer error does not identify which
allocation failed on Samsung. A fixed-size page-table pool is a hypothesis
supported by the fork's rationale, **not a measured root cause for this phone**.

## Read-only Galaxy prerequisites

Two bounded snapshots were taken without an owned VM running. The original
Android guest work/misc/FRP files still match their recorded SHA256 values.
Existing Debian storage and the unrelated trusted VM were not manipulated.

| Observation | Result | Interpretation |
|---|---|---|
| Host | Linux 6.12.30, 4 KiB pages | Order-9/2 MiB arithmetic applies |
| Kernel configuration | THP, compaction, tmpfs, kprobes/kretprobes enabled; hugetlbfs disabled | Some broad prerequisites exist; no proof of a compatible module ABI/hooks or usable pool |
| Top-level shmem / anonymous / defrag | `never` / `always` / `never` | Anonymous THP policy cannot be used to infer memfd THP policy |
| 16–1024 KiB sizes | Both anonymous and shmem controls `never` | Two separate policy families; enabling one would not enable the other |
| 2048 KiB sizes | Both `inherit` | Ordinary internal shmem follows its top-level policy |
| Free buddy blocks, orders 9 and higher | Zero | No immediately free 2 MiB blocks in these samples; does not prove reclaim/compaction can never create one |
| Reserve module | Absent | No reserved-page substitution available |
| MemAvailable | Approximately 1.8–2.2 GiB in the USB samples; later 2.7–2.8 GiB over Tailscale | The existing manager's 4 GiB + 768 MiB admission margin is not met in these samples |
| Host boundary | Same boot; Enforcing; read policy values unchanged | No host experiment or restart was performed |

The user accepts normal Android app reclamation. The last admission row describes
**current software behavior**, not a new requirement to keep all other apps alive
or an explanation of the historical Gunyah error. Earlier 1 GiB failures occurred
with substantially more available RAM, before the requested guest workload was
complete. Reclaimable host memory and hypervisor-owned resources are different
budgets. A 4 GiB start is not a useful next discriminator while real backing
orders and the mapping failure remain unresolved.

The issue on another SM8850 device,
[Gunyah #44](https://github.com/quic/gunyah-hypervisor/issues/44), reports similar
mapping errors despite 16–18 GiB available host RAM and possible teardown trouble.
It remains an external report, not proof of a leak, a fix or an identical Samsung
implementation. No reboot-based recovery was attempted here.

## Allowed alternatives and what they establish

1. **Ordinary `MADV_HUGEPAGE` advice on memfd:** already compared in diskless
   Bridge probes. Flag acceptance and zero `ShmemPmdMapped` do not prove a useful
   large folio; PTE-mapped folios also require independent page-flag accounting.
2. **VM-local anonymous backing:** already compared, including prefaulting. It
   still failed and had zero anonymous THP. It is not a production replacement
   for shared-memory device/backend contracts.
3. **Own tmpfs object with `huge=always`, plus bounded `MADV_COLLAPSE`:** top-level
   shmem `never` is different from emergency `deny`; the related
   [Linux shmem source](https://github.com/torvalds/linux/blob/v6.12/mm/shmem.c)
   checks the per-superblock policy. Bridge previously tried a detached private
   object, without attaching a mount to any host namespace. Its 2 MiB collapse
   requests failed `ENOMEM` and owned-page flags showed no compound pages.
   Virtual/file alignment must be explicitly verified in any future variant.
4. **Small folios:** do not confuse the 16 MiB allocation size of that helper with
   the 2 MiB folio size it requested. It did **not** establish that 16 KiB or
   64 KiB allocations are impossible. They are disabled by current policies.
   Ordinary [`MADV_COLLAPSE`](https://man7.org/linux/man-pages/man2/madvise.2.html)
   is not a caller-selected 16/64 KiB allocator. The reviewed Android 6.12
   [`madvise_collapse`](https://android.googlesource.com/kernel/common/+/refs/tags/android16-6.12-2025-06_r1/mm/khugepaged.c)
   rounds and iterates PMD-sized windows. Copying the fork's size-cascade loop
   would not by itself verify smaller folios on Samsung. There is a specific
   false-positive case in the reference: for a PMD-aligned start and a shorter
   range, the rounded start and end coincide. No collapse loop runs, and the
   zero-completed/zero-requested comparison can return success. Thus a successful
   sub-PMD call can be a no-op. This is a source-level observation, not a new
   execution result on Samsung; verify actual folio flags/order rather than the
   fork's return-code-based size counters.
5. **Pre-pinning:** the fork's
   [`pin.rs`](https://github.com/Droid-VM/crosvm/blob/849e05f12d5c8d4c36af8e15763986d242cd43b5/hypervisor/src/gunyah/pin.rs)
   uses `io_uring` buffer registration as a userspace long-term pinning path. Its
   optional preflight also uses the reserve module's device. Pinning can cause
   migration; it does not create huge folios or identify a later hypervisor map
   allocation. It has not been executed here and is not presented as a fix.

No host-setting-free method has yet produced verified large folios and passed
the failing workload on this Galaxy. The next useful change must first prove
backing size on a tiny owned allocation, retain ordinary shared-memory semantics,
and avoid the fork's global writes. A clean user-space VMM fork could isolate
those operations, but cannot manufacture unavailable contiguous memory or enlarge
an opaque hypervisor budget. No unreviewed fork/prebuilt was launched.

## Reproduce the non-invasive diagnosis

`tools/audit_memory_backing.py` reads a fixed list of policies and memory counters,
checks the explicit Galaxy/root identity, and compares boundary values before and
after. It does not start/stop a VM, allocate a reserve, write on the phone, install
software or change any policy. Missing fields remain unknown. It writes a new,
private **Mac** directory; raw boot IDs and configuration remain ignored artifacts.

```sh
python3 tools/audit_memory_backing.py \
    --adb "$ANDROID_SDK_ROOT/platform-tools/adb" --server-port 5038 \
    --serial "$GALAXY_ADB_SERIAL" --su \
    --target-mib 4096 --output artifacts/memory-audit-new
```

`--su` uses an already authorized host's root escalation when ADB starts as shell;
omit it with an existing root adbd. Do not change Tail ADB permissions or run
`adb root` to make the audit work. For offline analysis, use `--snapshot` instead
of `--serial`; `--failure-log` classifies an existing log's failing stage, without
printing addresses or claiming the precise exhausted resource. Tests cover wrong
device refusal, unknown policies, base-page-size-dependent buddy arithmetic,
missing data and distinction between folio provision and hypervisor map errors.
