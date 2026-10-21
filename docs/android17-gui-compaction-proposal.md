# Proposed one-shot GUI memory compaction comparison

Status: **approved, executed once; approval consumed**.
On 2026-10-21 the user answered 「追加の1回だけ許可する」 to this
specific45-second, single-write comparison. The three previous approvals
remain consumed; this approval is separate and cannot be reused.

The actual Bridge viewer has stopped safely before VMM execution on backing
THP checks at96, 368 and 192 MiB. The host remains healthy, Enforcing and taint 4608;
the six temporary shmem policies were restored each time. A readonly snapshot
showed about3GiB MemAvailable and scarce ordinary larger free blocks. This is
not evidence of a host crash or a root permission denial.

## Concrete proposed operation

1. Recheck the exact supported host/kernel, healthy taint, original boot/core/
   trusted-VM identities, all six `never` policies, and the1536 MiB memory floor.
2. Require no owned VM or allocation-pressure child. Acquire the existing VM
   owner and maintenance leases. Refuse any live Debian/user session.
3. Use one new durable O_EXCL receipt, distinct from all three spent receipts.
   Do not delete, reset or overwrite any approval record.
4. Through the existing reviewed 45-second owned-child supervisor, write exactly
   `1\n` once to `/proc/sys/vm/compact_memory`. No preceding pressure allocation,
   retry, cache-drop, persistent tuning, extra THP key or other sysctl write.
5. Retain immediate/delayed readonly memory and migrate-type counts and verify
   unchanged host health/protection. Compaction has no page-placement rollback;
   kernel stalls can outlive a userspace watchdog.
6. Only on a healthy postflight, use the existing separately approved16 MiB
   full-content handoff gate before one manually initiated1 GiB GUI VM attempt.
   Keep the6 GiB total budget,1536 MiB floor, all-THP admission and independent
  40-second restoration guardian. Preserve refusal results without weakening
   the gate or retrying automatically.

Compaction is host-wide runtime page relocation. It can stall other apps and
cannot guarantee improved VM backing or preservation of temporary root if a
host defect is triggered. It does not write kernel files/partitions, load a
module, change SELinux/protection policy or restart Android.

## Why another approval is required

The current repository [AGENTS.md](../AGENTS.md) records that the earlier two
renewed compaction comparisons were consumed and states:

> No further compact_memory write is authorized by these two spent receipts.

The user has now supplied the separate approval above. Keep its new spent
receipt even on failure; any subsequent compaction requires a new approval.

## Recorded outcome

The single write returned 2 bytes / errno 0 in 566 ms. Immediately observed free
blocks of at least 64 KiB increased from 256 KiB to 595264 KiB, including 582272 KiB
classified as CMA. CMA is not interchangeable with the ordinary movable pool;
these are transient snapshots and do not prove usable 1 GiB VM backing.
The16 MiB handoff passed 4096/4096 THP pages and complete content/PFN return.

The following manual GUI attempt passed complete 16 MiB batches through384 MiB
(400 MiB admitted in total) but refused the batch starting at400 MiB before VMM
execution. Initial sampling stopped at 2544/4096 present pages; the existing
bounded PTE refresh observed 3424 present and 3408 THP pages. This is incomplete
backing evidence, not proof that the first missing PTE means physical allocation
failure. The conservative gate stayed intact. Both shmem restorers completed;
original boot/core/trusted VM, host health/protection and source disks passed.

No guest frame, touch, hide/show or reconnect result was obtained in this round.
The spent receipt is retained. This does not authorize another compaction.
