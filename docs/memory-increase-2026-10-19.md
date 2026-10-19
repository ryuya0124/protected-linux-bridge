# Larger-memory follow-up, 2026-10-19

The user authorized further VM experiments while retaining the **6 GiB maximum**
and host protections. No disk was attached to these diagnostic guests. Current
Android/Debian disks and the unrelated trusted VM remained separate.

## Observations

Ordinary preparation touched **6112 MiB**, completed sampled readback with zero
errors and released that mapping. Together with its 16 MiB resident target and
16 MiB reserved overhead, its explicit budget was 6144 MiB. The 1536 MiB
MemAvailable floor was retained. This is host allocation evidence, not guest RAM
usage. No host compaction/cache-drop control was written.

| Configured RAM | Requested guest workload | Change | Result |
| --- | --- | --- | --- |
| 5120 MiB | 4096 MiB | Original shared refault, fresh bounded preparation | Gunyah map error 10 after the 448 MiB write-progress marker |
| 512 MiB | 256 MiB | Base-page prefix and tail, reverse middle prefault | Complete readback; 65536 distinct guest pages, 262144 KiB max RSS |
| 3072 MiB | 2048 MiB | Base-page prefix and tail, forward middle prefault | Reached the 1792 MiB write-progress marker, then Gunyah map error 10; no complete readback |
| 3072 MiB | 2048 MiB | Same margins, reverse middle prefault | No guest-init output; donation error 111 and host page-cache warnings |

The forward 3 GiB trial observed **745472/745472 middle pages with THP flags**,
with zero flag-read errors and no VMM swap at the sampled failure interval. Its
compound-head count was 139173. Nevertheless, the 2 GiB guest workload failed.
Thus complete small-THP observations do not by themselves establish a usable
large guest, nor identify a particular hypervisor resource limit. A 1792 MiB write
marker is **not** a successful 1.75 GiB readback/residency test. The largest fully
verified historical guest workload remains 1536 MiB; 4 GiB remains unachieved.

New relative 256 MiB bands correlate guest placement and backing observations,
without publishing guest/host PFNs. The guest initially used its higher RAM zone,
then lower RAM. Reversing prefault placement changed the compound-head distribution,
but did not establish a better working configuration. A 4480/4096 MiB comparison
was prepared in source to reduce unused backing, but was **not run** after the
warning below.

## Host warning and immediate response

The reverse 3 GiB trial produced **242 new `BUG: Bad page cache` messages** in the
captured kernel tail. The stacks passed through `filemap_unaccount_folio`,
`shmem_undo_range`, `shmem_fallocate` and the trial's `fallocate` call. The reason
was `still mapped when deleted`; the sampled folios were order 2. The warning
occurred during mapped-memfd hole punching, before the subsequent guest donation
failure. This is a host page-management failure, not a guest Android crash. The
precise vendor-kernel defect and lasting effects are not established.

**No further VM or allocation-pressure trial was started after detection.**
The owned VM exited, the three shmem policies were verified `never`, the owner lock
was available, host boot identity/main system process IDs remained unchanged,
SELinux stayed Enforcing and the unrelated trusted VM retained its PID. These
checks do not prove the host memory state is undamaged. The observed kernel taint
included BAD_PAGE; no clear-before-trial taint baseline is claimed. No taint bit
was cleared, no host setting was used to hide the warning, and Android was not
restarted. This boot is not used for further large-memory load tests.

## Source and app changes

- Retire the mapped-hole-punch helper. It now returns `ENOTSUP` without changing
  the supplied mapping or file; the executable old implementation remains only
  in Git history. The supervisor/runner reject the retired stages before load.
- The app's 1.5 GiB production variant uses **prefault-only admission**. It does
  not include the refault helper or punch a hole. Incomplete THP coverage refuses
  startup; no fallback invokes the retired path.
- Before new diagnostic load or the app's guarded large Android mode, inspect
  `/proc/sys/kernel/tainted`. BAD_PAGE or OOPS flags, malformed/unreadable state
  refuse the launch before any policy change or allocation. Expected root-related
  out-of-tree/warning flags alone are not treated as BAD_PAGE. Absence of these
  bits is only a preflight condition, not proof of kernel/hypervisor health.
- App 0.4.0-preview.6 explains this refusal. The current phone is expected to
  refuse 1.5 GiB; its 512 MiB choice and existing disks remain available.

The changed prefault-only 1.5 GiB path is compiled and tested for admission, but
**has not been boot-validated on a clean host session**. Prior preview.5 successful
screenshots are historical evidence for its older runtime, not verification of
this change. Do not bypass the new guard or replay the retired path to obtain a
larger number. Future work needs a clean permitted test session and a replacement
that never deletes a still-mapped folio; it must preserve the host boundary.

[Sanitized measurements](data/memory-increase-2026-10-19.json) ·
[Earlier app integration](android-large-profile.md) ·
[Host prohibitions](host-boundary.md)
