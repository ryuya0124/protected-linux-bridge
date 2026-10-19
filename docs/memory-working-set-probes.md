# Configured RAM versus newly mapped pages

Measured on the selected Galaxy on **2026-10-19**. A **4 GiB-configured protected
Linux VM now boots and passes a 256 MiB page-marker workload**. This is a new
result, but **4 GiB resident guest RAM and a stable large Android desktop remain
unverified**. The historical Gunyah mapping failure is not fixed.

These developer probes do not change the released APK, normal resource-admission
checks or persistent Debian disks. Machine-readable, sanitized results are in
[data/memory-working-set-2026-10-19.json](data/memory-working-set-2026-10-19.json).
Raw console/kernel logs, identifiers and generated DTBs remain private.

## Actual VM runs

All seven runs used the installed original crosvm, its original shared memfd
backing, the existing Linux 6.18.57-plb1 guest kernel and a diskless diagnostic
initramfs. No graphics, network, vsock or persistent disk was attached. Only one
owned VM ran at a time, under the common ownership lock.

| VM configuration | Distinct workload allocation | Rounds over the same allocation | Outcome |
|---|---:|---:|---|
| 768 MiB | 384 MiB | 8 | Every round passed; guest powered off normally |
| 4096 MiB | 64 MiB | 1 | Passed; generated guest DTB captured |
| 4096 MiB | 256 MiB | 1 | Passed |
| 1024 MiB | 768 MiB requested | 1 | Gunyah vCPU failure after the 448 MiB progress marker |
| 4096 MiB | 256 MiB | 2 | Both rounds passed; VMM accounting sampled |
| 1024 MiB | 768 MiB requested | 1 | Same failure after the 448 MiB marker; accounting sampled |
| 512 MiB | 256 MiB | 1 | Passed after the failure; guest powered off normally |

The progress interval is 64 MiB. The last marker is **not** the exact failing
allocation size. Both failing VMMs returned exit status zero despite their vCPU
failure. The result checker correctly rejected them because readback/completion
was missing and a vCPU failure was logged.

One preparatory launch used an older runner that rejected the new arguments
before starting a VM (exit 20). It is excluded from the seven-run matrix. Its
remaining old console file is not evidence of a new guest run. The current
wrapper requires the runner's matching launch marker before accepting results.
The first file-staging attempt also refused a lock operation before installation;
installation was then performed under an inherited standard-input lock. No busy
owner lock was bypassed.

### What the workload verifies

The guest writes a changing marker at every 4 KiB boundary and reads those
markers back after each round. It keeps the same allocation resident between
rounds: no discard, unmap, swap disk or new workload allocation is introduced.
This is page coverage and marker integrity, **not** a full-byte memory test or
bandwidth benchmark.

For the 384 MiB / eight-round run:

- Guest maximum RSS remained **393216 KiB**.
- Minor faults were **98314** after round one and unchanged through round eight.
- Major faults and marker errors remained **zero** in every round.
- Repeated coverage was 3072 MiB, but distinct workload allocation was only
  **384 MiB**. It must not be described as a successful 3 GiB resident allocation.

This short experiment supports reuse being stable within its measured footprint.
Together with the larger first-pass failures it points toward **new backing /
GPA mappings**, rather than elapsed time or repeated access alone. It does not
prove a particular hypervisor allocation limit or rule out long-running leaks.

### What the 4 GiB configuration verifies

The generated DTB, checked with libfdt's `fdtget`, contains:

- `/memory/reg`: base **2 GiB**, size **4 GiB**.
- `/gunyah-vm-config/memory/base-address`: **2 GiB**.
- `/gunyah-vm-config/memory/size-max`: **30 GiB**.

Guest `sysinfo` reports **3994516 KiB total** and **3976980 KiB free** before the
workload. Both 64 MiB and 256 MiB marker workloads completed. No DTB property was
patched. This rules out rejecting the tested configuration merely because the
RAM setting exceeds 512 MiB or 1 GiB. It does **not** mean all 4 GiB has been
physically backed, pinned or successfully mapped by Gunyah.

## VMM accounting adds a separate constraint

The instrumented runner samples only its own directly created VMM once per
second. Values are sampled maxima, not an exact snapshot at the failure.

| Run | `VmLck` max, KiB | `VmRSS` max, KiB | `VmPin` max, KiB | `VmSwap` max, KiB |
|---|---:|---:|---:|---:|
| 4096 MiB / 256 MiB / two rounds, pass | 473036 | 494648 | 0 | 0 |
| 1024 MiB / 768 MiB requested, failure | 568328 | 612448 | 0 | 0 |
| 512 MiB / 256 MiB recovery check, pass | 329240 | 351644 | 0 | 0 |

The process's displayed soft/hard `Max locked memory` limit was 65536 **bytes**,
yet `VmLck` reached hundreds of MiB. That displayed limit alone does not explain
a failure near this working-set size. No rlimit was raised. `VmPin=0` does not
mean Gunyah pinned no pages: these proc counters describe different kernel
accounting mechanisms. Likewise, `smaps` `Locked: 0` is not evidence against the
independently observed `VmLck` accounting. None is an exact hypervisor heap gauge.

Sampled `AnonHugePages` and `ShmemPmdMapped` stayed zero. These counters alone do
not enumerate every possible folio layout; the earlier owned-page flag and
collapse experiments remain the direct small-allocation evidence.

The host kernel recorded `Failed to map guest address ...: 10`, followed by
`Failed to provide folio ...: -12`. This matches the earlier classified
`addrspace_map` failure, rather than a newly demonstrated shortage of guest
virtual address space. The precise exhausted internal resource remains unknown.

## Source interpretation and remaining test gate

The pinned public hypervisor's
[range-map implementation](https://github.com/quic/gunyah-hypervisor/blob/254727746fce809aba0e629a13711a954b246968/hyp/misc/range_map_gpt/src/gpt.c)
allocates tree levels from a partition and can merge compatible, completely
filled levels. Its
[page-table allocation path](https://github.com/quic/gunyah-hypervisor/blob/254727746fce809aba0e629a13711a954b246968/hyp/mem/pgtable/armv8/src/pgtable.c)
uses a separate VM-page-table allocation attribute for stage 2. These provide
plausible metadata-allocation paths, **not** proof of which one failed in
Samsung's binary or a measured fixed mapping quota.

The pinned public Resource Manager also has a
[heap statistics RPC implementation](https://github.com/quic/gunyah-resource-manager/blob/7e9344582eda391f45eb3d54261f45e069713f88/src/heap_mgnt/heap_mgnt.c).
Its existence in source does not establish an accessible/supported Android
userspace ABI on this phone. The examined Gunyah sysfs directories did not expose
such statistics, and debugfs was not mounted. No mount, raw RPC injection,
unknown ioctl, root-partition heap donation or global policy write was attempted.

The next meaningful large-workload comparison needs **verified different physical
backing/folio supply**, or a supported isolated mapping strategy with a measurable
change in metadata demand. Increasing only configured capacity has now passed
its small-workload test and is not a demonstrated cure. Repeating identical
768 MiB failures would not identify the internal allocator.

See the [DroidVM comparison](droidvm-hugepage-comparison.md) and
[allocation follow-up](memory-provisioning-followup.md) for why global THP changes,
reserve modules, successful short collapse return codes and ordinary 4 GiB
Android allocations are distinct issues. No host-setting-free correction has
yet passed the failing large workload.

## Reproduce the bounded comparisons

Build only:

```sh
python3 tools/build_memory_probe.py --sdk "$ANDROID_SDK_ROOT"
```

Explicitly validate/install the root-private diagnostics described in the
[allocation follow-up](memory-provisioning-followup.md). Never run the guest PID1
binary directly on Android. Then, on the explicitly selected device:

```sh
python3 tools/run_memory_probe.py \
    --adb "$ANDROID_SDK_ROOT/platform-tools/adb" --server-port 5038 \
    --serial "$GALAXY_ADB_SERIAL" --su --initial-mount-namespace \
    --kernel linux618 --memory 768 --touch 384 --rounds 8 \
    --output artifacts/memory-working-set

python3 tools/run_memory_probe.py \
    --adb "$ANDROID_SDK_ROOT/platform-tools/adb" --server-port 5038 \
    --serial "$GALAXY_ADB_SERIAL" --su --initial-mount-namespace \
    --kernel linux618 --memory 4096 --touch 64 --dump-fdt \
    --output artifacts/memory-working-set
```

The 4 GiB diagnostic mode deliberately permits only original shared backing and
at most 256 MiB of workload. Its admission heuristic allows 512 MiB of boot
allocations plus 768 MiB of host headroom, rather than claiming 4 GiB residency.
This is **not** a hard resident-memory cap or an OOM guarantee and is not applied
to production VM admission. Other diagnostic sizes retain their previous checks.
Repeated page coverage is bounded to 3072 MiB and eight rounds so it fits the
existing 25-second runner deadline. The owned VMM receives `oom_score_adj=1000`,
a per-process preference for reclaiming this diagnostic rather than normal apps.
No process is mlocked by the helper. Parent-death and timeout cleanup target only
its direct VMM child; kernel/hypervisor hangs cannot be given an absolute
userspace recovery guarantee.

All seven runs retained the same host boot ID and SELinux Enforcing. Final
read-only policy checks retained shmem `never`, anonymous THP `always`, defrag
`never` and disabled smaller THP orders. No free 2 MiB buddy block was reported in
the final snapshot. No reserve module, host kernel/partition change, Android
restart, manual killing of other apps or unrelated-VM operation was used.
Existing Debian disks were never opened by these probes.
