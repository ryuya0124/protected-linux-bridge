# Memory follow-up: allocation, folios and the actual guest address range

Measured on the selected Galaxy on 2026-10-19, following the
[DroidVM comparison](droidvm-hugepage-comparison.md). **A 4 GiB host-process
allocation passed. Subsequently, a 4 GiB-configured protected VM booted and
passed a 256 MiB page-marker workload. Full 4 GiB resident operation remains
unverified:** see the [working-set probes](memory-working-set-probes.md).
The historical Gunyah map failure remains unresolved. The released APK and normal
VM settings have not been changed by these developer diagnostics.

## What is now separated

| Question | New evidence | Remaining limit |
|---|---|---|
| Can a process obtain ordinary RAM beyond 512 MiB? | Wrote all pages of 4096 MiB; about 4 GiB RSS while held; sampled readback passed | This is Android process RAM, not successful Gunyah donation/mapping |
| Can shared backing become THP-eligible without global policy writes? | A detached tmpfs object with `huge=always` reported `THPeligible=1` | No actual 2 MiB folio was obtained |
| Does `MADV_COLLAPSE` reach an allocation attempt? | Full 2 MiB requests returned `ENOMEM`; collapse-allocation-failure counter rose | Counters are host-wide; exact vendor instruction bodies are unverified |
| Does successful 64 KiB collapse prove a 64 KiB folio? | No: zero return, no THP/compound pages and no collapse-allocation event | Never use this return code as a folio-size measurement |
| Is the VM secretly capped to a 512 MiB address layout? | Captured 1 GiB RAM configuration, 30 GiB Gunyah layout, and approximately 954 MiB usable guest RAM | No claim of a successful large touched workload |
| Can detached tmpfs simply replace crosvm memfd? | Its seal state prevents adding the required grow/shrink seals | Constructor/backend contracts need a separate design |

All runs used an unchanged host boot and SELinux Enforcing. No host kernel/module,
partition, policy, global THP/compaction/cache-drop, root-method or Android restart
operation was used. No unrelated VM was stopped. The idle owned Bridge manager
was verified to have no child processes and stopped through its existing
`--stop` protocol before allocation tests acquired the shared ownership lock.
The test VM later powered off **only its guest** through the existing PID1 probe.

## Aligned, resident 16 MiB tests

`tools/backing_allocation_probe.c` now reserves an owned virtual range before
mapping its interior at a 2 MiB boundary. Shared mappings use file offset zero.
This removes virtual/file-offset misalignment as an explanation of these samples.
Each baseline populated all 4096 base pages before measurements, checked the
process's THP-disable state, sampled only its own pagemap/page flags, and read back
the page markers afterwards.

| Backing | Process THP disabled | THP eligibility | Observed compound/THP pages | Full 2 MiB collapse |
|---|---:|---:|---:|---|
| Private anonymous | 0 | 1 | 0 | Both requests: `ENOMEM` |
| Ordinary memfd + huge-page advice | 0 | 0 | 0 | Both requests: `ENOMEM` |
| Detached private tmpfs, `huge=always` | 0 | 1 | 0 | Both requests: `ENOMEM` |

The same matrix was repeated with fresh resident targets after the larger
allocation/release trial, with roughly 3.65 GiB MemAvailable in the before
snapshot. All 4096 pages were present at the measured stages, and readback passed.
The large-page outcome remained unchanged.

For every measured full collapse request in those comparisons,
`thp_collapse_alloc_failed` increased by one. The corresponding intervals showed
three or four additional `compact_fail` events, no `compact_success`, and no
successful collapse allocation. These are **system-wide deltas**, not private
per-process counters; concurrent activity cannot be excluded. Their repeated
alignment with the requests supports high-order allocation failure. It does not
identify every physical blocker or the hypervisor's later allocator.

The related [Android 6.12 collapse code](https://android.googlesource.com/kernel/common/+/refs/tags/android16-6.12-2025-06_r1/mm/khugepaged.c)
uses `GFP_TRANSHUGE` for explicit collapse, counts failed huge-page allocation,
and maps that scan result to `ENOMEM`. A cgroup charge failure has a different
reference result. Thus top-level `defrag=never` is **not sufficient evidence**
that explicit collapse never attempts compaction. No defrag setting was changed.

Conversely, `THPeligible=0` on ordinary memfd describes normal eligibility; the
explicit collapse requests still reached an allocation-failure path in these
samples. Treating shmem `never` as an absolute rejection of every explicit
collapse operation would be inaccurate.

### Actual short-range no-op

A 2 MiB-aligned 64 KiB request returned zero on each backing. Owned-page flags
still showed zero THP/compound pages, and neither collapse-allocation counter
increased in that interval. This reproduces the reference's zero-whole-PMD-window
case on the device. It rules out using the fork's sub-PMD return-code cascade
alone to prove smaller folios.

Physical observations now distinguish forward and reverse PFN runs in virtual
address order. Adjacent physical frames mapped in reverse order are **not** a
forward contiguous translation and are not a huge folio. The classifier requires
a complete aligned 512-page window, readable flags, the correct head/tail flags,
THP membership and forward PFN order before counting a sampled 2 MiB folio.
Page flags can race with migration; a snapshot is not a long-term pin or a
promise about pages at subsequent Gunyah donation. No PFNs or addresses are emitted.

## The bounded 4 GiB host allocation

The user explicitly permits normal Android reclamation of other apps. One valid
pressure trial allocated at most 4096 MiB of anonymous RAM in 64 MiB batches,
filled each page with a nontrivial pattern, sampled each page's first word for
readback, and released the entire mapping. It did not send signals to other apps
or change any system reclamation control. Which apps, if any, Android reclaimed
was not individually measured.

The worker set **its own** `oom_score_adj` to 1000 so it could be selected as an
OOM victim. A separate direct parent supervised only its worker; the opt-in mode
has a 30-second parent deadline, a worker alarm and a 512 MiB available-memory
floor before each next batch. The maximum is a test cap, not a recommendation to
keep 4 GiB resident or run this repeatedly. Kernel stalls cannot be given an
absolute userspace recovery guarantee.

| Measured point | Value |
|---|---:|
| Initial MemAvailable snapshot | 3426212 KiB, approximately 3.27 GiB |
| Written allocation | 4096 MiB |
| RSS while holding it | 4195084 KiB, approximately 4.00 GiB |
| Anonymous THP while holding it | 0 |
| Sampled readback errors | 0 |
| MemAvailable immediately after release | 5810912 KiB, approximately 5.54 GiB |

The small pre-existing tmpfs target had been swapped out during pressure. Its
immediate subsequent collapse requests still failed, but those observations are
**not presented as a fully resident target comparison**. The fresh resident
post-pressure matrix above supplies that separate check. The current code also
repopulates/checks a surviving target before a future opt-in combined test; that
combined repopulation path has not been rerun with another 4 GiB pressure cycle.

### Excluded preparatory attempt

The first preparatory pressure helper imposed a fixed 6 GiB `RLIMIT_AS` and failed
before useful pressure measurement. It is excluded from the successful result.
A later small helper showed about 10.3 GiB `VmSize` with only tens of MiB RSS:
reserved virtual address space is not resident RAM. The fixed address-space
restriction was removed. The actual allocation remains bounded by fixed code
limits and the watchdog; no process's global/system limit was changed.

The watchdog has real subprocess tests for success, signaled exit, invalid PID
and a worker blocking alarms. A sibling process remains alive in the timeout
case. It never signals a process group or a PID found through process-name scans.

## Why the local tmpfs result is not a drop-in fix

A sealing-enabled memfd accepted grow/shrink/seal seals and read back mask 7.
The detached tmpfs file began with `F_SEAL_SEAL` (mask 1), and adding grow/shrink
seals failed with `EPERM`; its mask stayed 1. Those operations affected only the
new diagnostic file objects.

The reviewed [crosvm Linux shared-memory finalizer](https://github.com/Droid-VM/crosvm/blob/849e05f12d5c8d4c36af8e15763986d242cd43b5/vm_memory/src/guest_memory/sys/linux.rs)
requires those seals and propagates their failure. Returning a detached tmpfs FD
from a memfd interposer would therefore break its contract. No fake successful
`fcntl`, seal bypass or production backing replacement was added.

An explicit collapse request on the **original** memfd would preserve descriptor
identity and sealing, but it already encountered high-order allocation failure
here. Eligibility alone does not supply contiguous pages. The related
[shmem implementation](https://github.com/torvalds/linux/blob/v6.12/mm/shmem.c)
also separates per-superblock policy from per-order anonymous-shmem controls.
Both smaller anonymous and shmem orders remain `never` on this Galaxy; no control
was changed to make an unverified smaller-folio path appear supported.

## Actual guest DTB and usable RAM

The installed original crosvm's supported `--dump-device-tree-blob` option was
used in one diskless 1024 MiB / 64 MiB readback probe, with the existing pinned
Linux 6.18.57 guest kernel. No alternate VMM, interposer, GPU or persistent disk
was attached. The 4415-byte DTB was obtained through authorized root, parsed with
`libfdt`'s `fdtget`, and kept private.

Its selected properties describe:

- `/memory/reg`: base 2 GiB, length **1 GiB**.
- `/gunyah-vm-config/memory/base-address`: **2 GiB**.
- `/gunyah-vm-config/memory/size-max`: **30 GiB**, ending at 32 GiB.
- Guest `sysinfo`: **977044 KiB total**, approximately 954 MiB usable after reservations;
  **962364 KiB free** at the reported early point.

The guest completed the 64 MiB touch/readback with zero errors, emitted its
completion marker and shut down. This verifies configuration and a small touched
workload, **not** the previously failing 768 MiB workload. The subsequent
[4 GiB configuration experiment](memory-working-set-probes.md) is recorded
separately and still does not establish full resident operation.
No DTB size property was changed. A 512 MiB layout cap does not explain this
captured configuration. A larger IPA range is not a larger allocator budget.

The pinned public [Resource Manager parser](https://github.com/quic/gunyah-resource-manager/blob/7e9344582eda391f45eb3d54261f45e069713f88/src/vm_config/vm_config_parser.c)
and [configuration path](https://github.com/quic/gunyah-resource-manager/blob/7e9344582eda391f45eb3d54261f45e069713f88/src/vm_config/vm_config.c)
validate and clamp the IPA layout; they are reference source, not a proven match
to Samsung's RM. Separately, the public
[heap-add path](https://github.com/quic/gunyah-resource-manager/blob/7e9344582eda391f45eb3d54261f45e069713f88/src/vm_memory/vm_memory.c)
adds memory to the RM root partition heap. That is not equivalent to increasing
one guest's `--mem`, and it is not an established isolated, supported userspace
operation on this phone. No heap donation, unknown ioctl/hypercall, RM modification
or Gunyah permission/protection change was attempted.

## Reproduce developer diagnostics

These helpers are not APK features and are not automatically installed or run:

```sh
python3 tools/build_memory_probe.py --sdk "$ANDROID_SDK_ROOT" --allocation-research
```

The manifest includes the page classifier and watchdog headers. A developer must
explicitly validate/install the diagnostic binaries in root-private
`files/kernel-probes`; never execute the guest PID1 binary directly on Android.
No existing Debian disk is an input.

The default backing helper accepts `anon`, `shared` or `detached` and uses only
16 MiB. The additional `detached reclaim-4g` mode is **explicit opt-in**, can cause
normal app reclamation, and was used only under the user's RAM-use authorization.
Never add it to APK startup or treat host-allocation success as a VM pass.

The diskless runner now supports captured DTB and root-shell transports:

```sh
python3 tools/run_memory_probe.py \
    --adb "$ANDROID_SDK_ROOT/platform-tools/adb" --server-port 5038 \
    --serial "$GALAXY_ADB_SERIAL" --su --initial-mount-namespace \
    --kernel linux618 --memory 1024 --touch 64 --dump-fdt \
    --output artifacts/memory-fdt
```

`--initial-mount-namespace` changes only the launched command's mount namespace
so an app-origin root broker can see this app's actual private files. It does not
mount/remount anything. The runner refuses a busy owner lock; it never kills a
manager or unrelated VM to make room. DTB capture is refused with the anonymous
interposer, uses the installed VMM, and writes a fixed private output. Guest logs
and DTBs are retrieved through the authorized root command rather than assuming
that `adb pull` has access to private app data.

The related [Linux Gunyah memory implementation](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/blob/8e66bb68fe6735e93ef5dd59c184cf6341b0098d/drivers/virt/gunyah/vm_mgr_mem.c)
uses the VM's private-extent resource ticket for private folio donation. Dividing
userspace bindings into more regions does not by itself create larger folios or
establish a larger hypervisor heap. The reviewed ordinary crosvm path already
uses its LEND operation for protected private RAM. A chunking change needs a
specific demonstrated setup-array failure to address; it is not a verified cure
for this later address-map failure.

The next gate is still **verified large-folio supply or a supported per-VM mapping
strategy that reduces the observed hypervisor resource demand**. More ordinary
RAM, a larger `size-max`, a successful short collapse call, or an accepted huge-page
flag alone is not that gate. No host-setting-free correction has yet passed the
failing large touched workload.
