# Unexecuted memory-policy experiment

Status: proposal only. **No writes to these settings have been made.** The
released APK does not include this policy change. The user's host-kernel boundary
must be clarified before any execution; it is not part of the current authorization.

Update, 2026-10-19: **suspended under the user's explicit instruction to keep
host settings unchanged during the DroidVM comparison.** RAM usage and ordinary
Android reclamation are permitted; that does not approve either sysfs write.
The proposed 16 KiB setting below is for anonymous mappings. The Galaxy also
exposes a separate per-size `shmem_enabled` policy, currently `never`; changing
anonymous policy would not enable small shared-memory folios for normal crosvm
memfd backing. See [the source comparison](droidvm-hugepage-comparison.md).

The later [three-key shared-memory proposal](small-shmem-policy-proposal.md)
avoids the global defrag change and retains original memfd backing. It is also
unapproved. The older two-key controller's independently reopened child owner
lock would conflict with the supervisor; its real application mode is now
disabled rather than presenting it as a working trial. Its mock restoration
checks do not establish that real allocation handoff.

## Concrete trial

| Host runtime setting | Observed current value | Proposed temporary value |
|---|---|---|
| `/sys/kernel/mm/transparent_hugepage/hugepages-16kB/enabled` | `never` | `madvise` |
| `/sys/kernel/mm/transparent_hugepage/defrag` | `never` | `madvise` |

This would enable 16 KiB anonymous folios for mappings requesting huge-page advice
and permit direct reclamation/compaction for advised allocations. **Both are
system-wide settings:** other advised host mappings can be affected during the
trial. They are not confined to the VM even though the test allocation is.
No host kernel binary/patch/module, SELinux, AVB/Gunyah permissions, partition,
Android property, reboot or unrelated-VM operation is proposed.

The first stage would allocate only 16 MiB in a private process, check owned-page
compound/THP accounting and read back data. It would not start a larger VM. Only
if that establishes useful backing would a separately bounded diskless VM test
follow. A successful allocation is not proof that the Gunyah fault is fixed.

## Prepared supervisor and remaining approval

`tools/memory_policy_probe.c` implements the fixed-path supervisor. `--plan` only
reads/validates the current state. The real-setting application mode has not been
executed. It is excluded from the APK and only built with the explicit development
builder option `--policy-proposal`.

A compile-time mock uses two regular files in the app-private diagnostic directory
instead of sysfs. Both normal completion and deliberate SIGKILL of the owned test
controller restored those mock files to their original values through the independent
guardian. The real host settings stayed `never`. A guardian timeout kills only its
still-parented owned controller before attempting restoration, preventing that
controller from reapplying a setting afterwards. The first real trial still requires
explicit authorization; mock success is not a safety guarantee on a hung kernel.

The required invariants are:

- Read and validate both original settings; refuse unexpected paths/values.
- Hold the existing Bridge VM ownership lock, with no owned VM running.
- Use a fixed, small allocation and a short independent supervisor timeout.
- Restore both original values on normal completion, error and handled signals.
  An independent guardian must attempt restoration if the test process disappears.
- Check restoration and unchanged host boot ID/SELinux; do not claim absolute
  restoration guarantees after all privileged supervisors are killed or root is lost.
- Never use a global cache-drop command, another app's files, CMA reservations,
  module loading, a policy bypass or a reboot as recovery.

## Why investigate this

The host's normal 2 MiB allocation/collapse requests failed on tiny owned
allocations, even after releasing only this app's immutable Android image cache.
The latter freed roughly 347 MiB of cached pages without changing any file, but
higher-order buddy blocks remained absent and the owned allocation still used
only base pages. A per-object detached tmpfs with `huge=always` did not produce
large folios either. No mount was attached to any host namespace.

The running host config supports THP/compaction but has no `CONFIG_HUGETLBFS`.
Its smaller anonymous THP sizes are set to `never`; the existing private-process
advice cannot override that policy. Kernel type metadata also confirms the actual
Gunyah error enum: `GUNYAH_ERROR_NOMEM = 10`, and exit reason
`GUNYAH_VCPU_EXIT_PAGE_FAULT = 3`. Hypervisor mapping-resource exhaustion remains
the failure observed; the exact internal exhausted resource is not established.

Reference behavior: [Linux 6.12 THP documentation](https://www.kernel.org/doc/html/v6.12/admin-guide/mm/transhuge.html),
[Linux 6.12 shmem implementation](https://github.com/torvalds/linux/blob/v6.12/mm/shmem.c).
A kernel-module pool implementation exists, but it changes host kernel memory
management and is excluded by the user's constraints; it has not been installed,
built for the phone, loaded or executed.
