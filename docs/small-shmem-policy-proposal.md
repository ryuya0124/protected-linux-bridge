# Approved small-shmem folio trial

**Explicitly approved by the user on 2026-10-19 for the three bounded settings
below.** At approval, only mock files and read-only plans had been exercised.
The measured results below distinguish host folio creation from VM success.
The existing two-key anonymous-THP/defrag proposal is not used.
This is a narrow exception to the restriction on host-wide THP changes.
RAM-use permission alone did not authorize it; explicit exception approval did.

## Exact proposed exception

Temporarily change only these three volatile sysfs values from `never` to
`advise`, then restore each to `never`:

```
/sys/kernel/mm/transparent_hugepage/hugepages-16kB/shmem_enabled
/sys/kernel/mm/transparent_hugepage/hugepages-32kB/shmem_enabled
/sys/kernel/mm/transparent_hugepage/hugepages-64kB/shmem_enabled
```

These are **host-wide** policies, not VM-local settings. Other shared mappings
requesting huge-page advice can be affected during the trial. Restoring policy
does not undo every allocation/layout decision another process made meanwhile.
No top-level THP/shmem policy, anonymous per-size policy or defrag setting is
included. No kernel/partition/module, SELinux, AVB/Gunyah protection, global
compaction/cache-drop, unrelated-VM, root-method or restart operation is proposed.

## Why this differs from the failed PMD experiments

The phone exposes separate shmem policies for smaller folios, all currently
`never`. The reviewed [Android common shmem source](https://android.googlesource.com/kernel/common/+/65994b7471f1fd37242ff4a5032a812f5c168ae6/mm/shmem.c)
recognizes unlinked memfd mappings as anonymous shmem, selects per-size advised
orders, and can fall back through allowed orders. Mount-level `huge=always`
alone does not enable disabled smaller orders. A short `MADV_COLLAPSE` success
also did not supply them in the measured baseline.

This trial requests 16/32/64 KiB folios at ordinary advised faults, without
requiring a successful 2 MiB allocation or changing the global defrag policy.
It may still fail or fall back to base pages. The public source is not verified
instruction-for-instruction Samsung source; vendor hooks may affect results.
Actual owned-page flags and readback, not policy text or a zero return, decide
whether useful larger backing was obtained.

## Prepared stages and restoration

`tools/small_shmem_policy_probe.c` is excluded from the APK. It has fixed paths,
requires the selected root Galaxy/Android 16 with SELinux Enforcing, refuses
unexpected original values, checks memory headroom and holds the common owner
lock. There is no caller-supplied executable, path or shell command.

1. **Read-only `--plan`: already executed.** All three current settings were
   verified `never`. This mode never writes them.
2. **16 MiB allocation stage: approved.** The fixed original-memfd helper
   advises its owned mapping, reports compound/THP flags and checks markers.
   The controller is bounded to 12 seconds; an independent guardian uses a
   20-second deadline and attempts restoration after controller/session loss.
3. **1 GiB VM / 768 MiB workload: approved, conditional on useful-backing evidence.**
   The fixed diskless protected VM uses original crosvm and `--hugepages`, no
   persistent disks/GPU/network. Its runner retains a 25-second deadline, the
   controller 32 seconds and independent guardian 40 seconds. A VM exit code zero
   alone is never a pass. The wrapper also requires restoration acknowledgements
   and unchanged before/after policy snapshots.

Larger working-set trials toward 4 GiB would still need bounded configurations
and verified backing, not simply a larger RAM setting. They are not silently
launched by this supervisor or the released APK.

The guardian restores on ordinary failure, handled signals and controller death;
its mock was also tested by deliberate SIGKILL. This cannot guarantee restoration
if all privileged supervisors are killed, root disappears, or the kernel hangs.
It never restarts Android as recovery. Existing folios are released by ordinary
owned-process/VM shutdown; policy restoration alone does not reclaim them.

## Executed mock tests

Compile-time mocks use three root-private **regular files**, not sysfs. Normal
completion verified that the owned child inherited the same lock description and
saw all three temporary mock values; controller and guardian restored all files.
Deliberately killing the controller also resulted in guardian restoration of all
three files. Real host settings stayed unchanged throughout.

`tools/diagnostic_owner_lock.h` validates the inherited descriptor against the
fixed lock inode, owner and permissions, reacquires the same lock description and
marks it close-on-exec. This avoids reopening a competing lock from the owned
allocation/VM child. Unit tests reject unrelated, closed and malformed
file descriptors and verify the parent retains exclusivity. Production manager
lock acquisition is unchanged. The older unexecuted two-key prototype did not
support this handoff and must not be used for the real trial.

## Build and read-only inspection

```sh
python3 tools/build_memory_probe.py --sdk "$ANDROID_SDK_ROOT" \
    --allocation-research --small-shmem-policy-proposal
```

This only builds. After explicit validation/installation in root-private
`files/kernel-probes`, `small-shmem-policy-probe --plan` only reads the current
three modes. **Do not invoke `--apply-approved-small-shmem` without the user's
explicit exception approval.** The approval and real executions are recorded
below; build-only and read-only modes do not imply authorization elsewhere.

## Actual approved trials, 2026-10-19

The real trials were serialized on the selected Galaxy. Raw addresses, kernel
images, VM consoles and device identifiers remain private. Selected aggregate
measurements are in
[data/small-shmem-trials-2026-10-19.json](data/small-shmem-trials-2026-10-19.json).

| Configuration                                                           | Measured outcome                                                                                        |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| 16 MiB sealed memfd                                                     | 2464/4096 pages had THP flags; zero marker readback errors; no sampled 2 MiB folio                      |
| Original VMM / 1 GiB RAM / 768 MiB workload / huge-page advice          | Failed before guest markers; later host evidence distinguishes donation error 111 from mapping error 10 |
| Keep first 64 MiB at base pages; prefault/refault original shared memfd | 512/256 MiB passed; 1024/768 MiB initially stopped after 576 MiB progress                               |
| Same 1024/768 MiB profile after an owned 4 GiB allocation/release       | Passed, zero errors; all sampled upper pages had THP flags                                              |
| 2048 MiB RAM / 1536 MiB workload, shared refault                        | Passed once: 393216 distinct guest pages, 1572864 KiB guest max RSS, zero readback errors               |
| 4096 MiB RAM / 3072 MiB workload, shared refault                        | Failed after 384 or 512 MiB progress; backing samples had many base-page fallbacks                      |
| 2048/1536 MiB, additionally requesting bounded collapse                 | Failed after 1472 MiB progress; three 2 MiB folios observed in this run                                 |
| 4096/3072 MiB, sparse collapse/reverse-prefault variants                | Failed either before guest output or after 384 MiB; donation error 111 also occurred                    |

The 1.5 GiB success is **resident guest memory evidence**, unlike configuring
4 GiB and touching only 256 MiB. It is not yet a stable 2 GiB profile: later runs
under different physical supply conditions failed. Progress markers are rounded
to 64 MiB and are not the exact failing allocation size. One byte per 4 KiB was
written and checked; this is page-residency/readback coverage, not an all-byte
RAM stress or throughput test.

The owned allocation/release uses ordinary private anonymous memory, a 512 MiB
MemAvailable floor, per-process OOM victim preference and an owned-child watchdog.
It never directly kills another app or writes cache-drop/compaction controls.
Normal Android reclamation may still affect other apps. The 6 GiB variant also
passed sampled readback, but did not make subsequent large VM tests reliably pass.

Explicit collapse sometimes obtained actual 2 MiB folios after reclamation;
other attempts all returned ENOMEM. A successful short-range collapse request
alone remains insufficient evidence. Sparse-seed and populated-collapse variants
are distinguished in the data, rather than pooled as identical trials.

Every completed approved VM trial checked controller/guardian restoration,
all three values back at `never`, unchanged boot ID and SELinux Enforcing. An ADB
reconnection/authentication interruption was resolved before subsequent trials;
no Android restart was used as recovery.

## Active-slot driver inspection

The active-slot boot partition was **read only**, extracting only its kernel
payload for offline analysis. No boot or kernel file on the phone was modified.
The 41228800-byte compressed payload SHA256 is recorded in the aggregate data.
An isolated Mac installation of
[vmlinux-to-elf 1.3.6](https://github.com/marin-m/vmlinux-to-elf) reconstructed
symbols for LLVM disassembly. Images, ELF output and address-bearing assembly
are excluded from Git.

The image's `gunyah_vm_provide_folio` calls memory donation before address-space
mapping; `gunyah_error_remap` explicitly maps hypervisor 10 to Linux -12. This
supports the observed map-error diagnosis beyond the earlier related-device
source comparison. It does not expose the closed hypervisor allocator or prove
which metadata resource is exhausted. This is analysis of the active-slot image,
not a read of runtime instructions or proof that every public source line matches.

The image's parcel conversion also walks each memory-entry length and each
folio's size. Therefore the simplistic zero-entry/folio-loop concern from an
older related-device source must not be asserted as a confirmed Samsung bug.
Donation error 111 remains a separate path, not another spelling of NOMEM.
Read-only forwarding of the original VMM ioctls established that the DTB is near
the end of the main RAM binding, rather than in its low 64 MiB. No ioctl was
modified, skipped, fabricated or repeated by that observer.

## Android guest comparison

The dedicated builder is:

```sh
python3 tools/build_android_memory_probe.py --sdk "$ANDROID_SDK_ROOT"
```

It builds fixed 1536/2048 MiB diagnostics, never installs or runs them. A separate
private Android profile uses offline copies of its writable qcow2/misc/FRP files
and the existing immutable base. The normal 512 MiB profile and all Debian disks
are retained. Original Android writable-file SHA256 values are checked before
and after trials.

The Android-only interposer is compiled separately, requires the pinned private
VMM and exact protected software-GPU/vsock/three-disk profile, and retains the
original MAP_SHARED memfd and FD/offset contract. It refuses anonymous RAM and
arbitrary external backends. It keeps 64 MiB boot margins at both ends, populates
them before the middle mapping, and refaults only the owned, not-yet-donated
middle. These margins are an experimental comparison, not a general proof of
boot-parcel correctness.

Only the first ten seconds of the Android trial use the approved three-key
policy. Controller and independent guardian then restore it while the owned VM
continues its bounded observation for up to two minutes. Restoring policy does
not split existing folios. The owner lock remains held for the entire VM lifetime.
The guest-only bootstrap repeats the pinned device/hardware/build/root checks,
sets up its existing virtual console HAL nodes and guest swap, as in the normal
512 MiB viewer. It never changes host hardware nodes or host swap.

The unmodified 2 GiB baseline and early low-THP shared-backing comparisons still
failed with the map error. A later 1536 MiB run with all sampled middle pages
THP-backed ran for its two-minute observation without a vCPU fault; the initial
read-only observer had omitted required guest HAL setup, so that run did not
complete Android boot. With the standard root-verified guest bootstrap restored, 1536 MiB Android
subsequently reached `sys.boot_completed=1`, retained its observed SystemUI and
SurfaceFlinger PIDs through the bounded observation, and returned a valid
480×800 lockscreen PNG. This is a short successful boot/display trial, not
production stability or hardware acceleration. Results are recorded in the
aggregate data and [Android display notes](android-guest-display.md).

The prepared 5120 MiB RAM / 4096 MiB workload was also executed. The host VMM
held approximately 5 GiB RSS, but the guest stopped after its 512 MiB marker with
the map failure, before completion. The diagnostic timed out during cleanup;
all approved settings were restored. Approximately 67 MiB of the final backing
sample was nonresident, so on-fault locking of only the owned mapping is a
separate prepared comparison, not an established fix. No memory from another
process is pinned or altered by that mode.

**4 GiB resident guest use remains unachieved.** No released APK applies these
host-wide settings, and the ordinary Android guest configuration remains 512 MiB.

### Process-local locking comparison

The small 512/256 MiB on-fault-locking comparison passed. The prepared 5120/4096
MiB comparison then set `mlock2(MLOCK_ONFAULT)` successfully on only its own RAM
mapping and reported zero VMM VmSwap during preparation, yet again hit the map
fault after the 448 MiB progress marker. It therefore does not establish swap as
the necessary cause or fix the 4 GiB goal. Pagemap samples can still race with
migration; on-fault locking is not an immovable physical pin. The lock and all
owned memory were released with the VMM.

The original 5120 MiB run's newly extended margin-log line was also rejected by
an overly literal wrapper check. The checker now accepts both old and extended
zero-result records, rejects nonzero/missing tail advice and has regression tests.
Historical raw assessments are preserved, with that parser issue identified in
the aggregate data. The independently observed vCPU/map failure still makes the
original large-memory run a failure.

Large prepared memory profiles have a 30-second native execution budget plus
termination grace inside the unchanged 32-second controller/40-second guardian.
Ordinary small-memory probes retain 25 seconds. These remain developer-only
experiments, not app startup or production memory policy.

### Larger ordinary allocation interruption

A separate opt-in 8 GiB-capped private allocation attempt was interrupted by loss
of the Tail ADB transport. The same 512 MiB available-memory floor and owned
watchdog were retained, but no complete output or verified reached size was
obtained. It is **not** counted as an 8 GiB allocation/readback pass. The planned
subsequent VM did not start and no small-shmem setting was changed in this attempt.
The phone's state after disconnection requires independent confirmation; loss of
ADB alone does not prove an Android reboot or identify the killed process.

## Checkpoint validation

119 local tests passed, including distinct guest-page tracking, inherited owner
lock rejection, failed-vCPU result handling and both margin-log formats. The
memory, Android memory, GLES and Vulkan diagnostic variants compiled with NDK
warnings treated as errors. The ordinary Android manager was separately compiled
from the previous commit and current source with the same flags/generated setup;
after stripping symbols and omitting build IDs, the two binaries were byte-identical.
Thus the compile-time diagnostic branches do not change that ordinary profile.
No replacement APK is published as a large-memory fix.
