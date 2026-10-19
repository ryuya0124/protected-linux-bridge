# Additional shmem THP proposal: 128 / 256 / 512 KiB

**Approved explicitly by the user later on 2026-10-19.** Before that reply,
existing permission covered only 16 / 32 / 64 KiB. Approval was obtained for
these additional three settings after preparing the separate build and read-only
plan. Read-only inspection confirmed all three existed and selected `never`. The compiled proposal's `--plan` also passed without
changing any of the six settings.

The proposed exception is temporary `never` to `advise` changes for:

- `/sys/kernel/mm/transparent_hugepage/hugepages-128kB/shmem_enabled`
- `/sys/kernel/mm/transparent_hugepage/hugepages-256kB/shmem_enabled`
- `/sys/kernel/mm/transparent_hugepage/hugepages-512kB/shmem_enabled`

The existing three approved sizes would also remain enabled only during that
bounded preparation. No other policy, anonymous THP, defrag/compaction/cache-drop
knob, module, kernel, partition, SELinux or hypervisor protection is changed.

The original reason to test was that a 64 KiB-only trial obtained 100% large-folio backing
in a 16 MiB sample, but only 51.9% THP coverage in a 4480 MiB preparation, then
failed at 2560 MiB write progress. With all three existing sizes enabled, a
3 GiB all-byte readback had succeeded, while protected 4 GiB had not. Larger
supply could reduce per-folio mapping demand. The subsequent six-size trials
completed protected 4 GiB all-byte readback twice; the exact exhausted allocator
is still unidentified and this is not a universal stability fix.

## Prepared mechanism and gates

The proposal uses the same explicit owner lock, ready pipe, fail-closed host
health check and independent restoration guardian as the measured three-key
experiments. It is a separate developer build of
`tools/small_shmem_policy_probe.c`, compiled with both
`PLB_PREALLOCATED_VM_POLICY=1` and `PLB_MEDIUM_SHMEM_PROPOSAL=1`. It has an explicit
six-path allowlist and a different application token:
`--apply-approved-medium-shmem`. The old three-key token is rejected by this
build. It is not part of the APK, and merely installing/building it is not
permission to invoke its application token.

The authorized sequence:

1. Verify the same host boot, Enforcing, no BAD_PAGE/OOPS and all six original
   policies `never`. Reject unexpected state rather than overwrite it.
2. Run one owned 16 MiB FD handoff with advice, inspect actual compound-head/THP
   counts and content retention. Policy guardian and controller must both
   restore all six values. A zero advice return is not proof of larger folios.
3. Only after useful larger-folio supply and restoration are established,
   proceed to diskless protected memory comparisons. Do not attach original
   Debian/Android disks. The 6 GiB combined diagnostic budget and 1536 MiB
   MemAvailable floor stay unchanged.
4. Each policy window is at most 40 seconds, with independent restoration on
   controller death and restoration immediately after preparation readiness.
   Abort the owned worker on new host warnings, invalid snapshots or timeout.
   Do not retry a failed VM automatically.
5. Verify policies, taint, SELinux, boot identity and Android core/unrelated VM
   process identities afterwards. Retain failed results as well as successes.

These are host-wide policies and can affect other applications that request
huge pages during the window. The guardian cannot guarantee restoration after
a host kernel hang; Android stopping/restarting and temporary-root loss remain
possible. A permission decision must acknowledge that scope. The proposal
includes no Android restart or kernel/partition/protection change.

The initial real-device 16 MiB gate passed: 4096 resident THP pages, 32 compound
heads (512 KiB folios), complete contents before/after exec, and all six policies
restored by both restorers. Host taint remained 4608 and SELinux Enforcing.
See the [follow-up results](preallocated-vmm-validation.md) for VM outcomes.
