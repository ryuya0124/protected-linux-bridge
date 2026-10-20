# One-shot host compaction comparison

**Status, 2026-10-21 JST:** the user subsequently approved one runtime compaction
operation, conditional on no host kernel-file or partition modification. That
one attempt is now consumed. It completed, followed by a successful 16 MiB
full-content handoff. See the [measured results](android17-runtime-validation.md#one-shot-host-compaction-2026-10-21-jst). No further write is authorized by this record.

The original proposal below described the earlier permission boundary. The
existing permission at that time covered only the six 16–512 KiB shmem advice nodes.
It explicitly excludes other global compaction/defrag/cache-drop writes. This
proposal does not change that permission. Do not execute it without new explicit
user approval. Kernel/module/partition/protection changes and host restarts
remain prohibited even if this proposal is approved.

## Evidence and scope

The inspected host enables CONFIG_COMPACTION and CONFIG_MIGRATION. In a saved
snapshot, the large free buddy blocks were CMA rather than ordinary Movable
blocks. Owned pressure/release increased useful mTHP coverage and allowed one
768 MiB protected CPU workload to finish, but 1280 MiB and the latest 1536 MiB
Android comparison still failed. Both fresh shmem and anonymous 2 MiB collapse
checks returned ENOMEM. During the anonymous comparison, the system-wide
compact-failure counters increased and success did not; concurrent host activity
prevents exclusive attribution of global counters to the probe.

An explicit host-wide full compaction attempt might scan/migrate more than the
process-local checks. It may still fail because pinned/unmovable pages cannot
be moved. Success is not promised. CMA is not relabeled or consumed as normal
RAM. The dedicated trusted/OEM CMA devices are not opened or used.

## Exact proposed operation

One write of `1\n` to `/proc/sys/vm/compact_memory`, with no simultaneous Bridge
VM or owned pressure allocation. This is a kernel runtime operation, not a
kernel replacement. It affects the entire host and can stall unrelated apps.
No persistent sysctl value is changed; physical page placement cannot be rolled
back. A timeout cannot guarantee interruption of an in-kernel compaction wait.

A dedicated root helper would require the exact Android 17 contract, owner and
maintenance locks, healthy taint/new-kernel-report state and 1536 MiB available
margin. It would snapshot buddy/migrate-type orders, meminfo and vmstat before
and after, supervise only its own child for at most 45 seconds and never signal
Android services or unrelated VMs. Failure/new health reports stop further load.
No reboot or hidden second attempt is permitted.

A successful write alone is not success. First compare ordinary free orders;
then, under the already approved independent six-key guardian, verify real large
folios and all contents in a fresh 16 MiB owned handoff. Only a positive gate can
admit a separate bounded diskless workload, which must finish nonce-bound full
write/readback. Android startup is a further independent qualification.

The reviewed helper is now `tools/host_compaction_probe.c`. It requires a
private generated boot-bound contract and a durable O_EXCL single-use receipt.
The successful write took 18 ms; this is not evidence of useful normal-pool
defragmentation. The receipt remains in place and must not be regenerated to
repeat the operation. The released APK does not expose or run this helper.
