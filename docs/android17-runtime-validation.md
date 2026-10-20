> Update: [the experimental app launch](android17-app-launch.md) now exposes the
> separate 1 GiB GLES runtime on explicit user request. The entries below retain
> their original chronology; earlier gate=false statements are historical.

# Android 17 runtime recovery: device evidence

2026-10-20, follow-up to the offline port and owned-display design. This is a
**developer validation record**, not a working Android guest release. A later
1 GiB developer session completed Android boot, real GPU home presentation,
Surface recreation and guest shutdown. The historical APK gate
remained false pending product integration and further qualification. Android 16 is frozen.

## Boundaries

All trials use the selected SM-S948Q / S948QOPU1BZI9 host, kernel 6.12.69, locked
bootloader and Enforcing SELinux. No host kernel/module/partition/protection,
restart, global display-service registration or compaction/cache-drop setting
was changed. The six already approved shmem advice nodes were used only with
independent, at-most-40-second restorers. The initial 16 MiB real-folio/data
handoff gate passed before VM comparisons. Policy restoration was verified.

Original Android 17 and Debian disks are preserved. Android trials use separate
copies. CPU comparisons are diskless. Root process health/owner locks, the
1536 MiB MemAvailable floor and combined 6 GiB diagnostic budget remain active.
Private raw logs, screenshots, kernels, ROMs and identifiers are not published.

## Display components that now work

A source-built Android 17 userspace VMM and renderer load successfully. The
sources are pinned to crosvm `c7dd8d6950912fa5e6fde7ec2d1616e0ddc090f8` and
gfxstream `b46f0d4b4545def09bdf30651d4bf848b2ba0c34`. The Cargo rutabaga base
is 0.1.76-alpha.0, archive SHA256
`988c93caed844e77bcb3c3988d89d249408aa772702b543c980d49b6665fb5fd`.
These are separate app-private binaries, not patches to the installed VMM.

A same-signer developer instrumentation test uses the real owned Binder
connection, real AHardwareBuffer metadata/FDs and an app-owned SurfaceView:

- A native Adreno GLES producer renders three independently generated colors.
- A host screenshot checks the actual presented center pixel for each color.
- Destroying/recreating the Surface replays the previous buffer; the next color
  is also checked after reconnection.
- The producer exits normally and the instrumentation restores the app's
  original `enabled` preference. No VM is started by this component test.

The new gfxstream renderer separately initialized GLES on **Adreno 840** and
completed teardown. Its feature override was `Vulkan:disabled`; upstream still
loaded Vulkan dispatch code. This is not Vulkan device/readback validation.
The retained renderer-window teardown warning is not hidden.

These results do not establish an Android guest frame, buffer reuse/fence
correctness, zero-copy, sustained frame rate or 120 Hz. The requested 120 Hz
work must request a supported rate only for the owned Surface and measure
actual presentation timing. Texture-update counts are insufficient.

## Android trials that did not complete

| Configuration | Observation | Result |
| --- | --- | --- |
| 4096 MiB, initial GUI attempt | RAM headroom refused before VMM | No boot |
| Inconsistent deployment | A changed root JAR digest was refused | No allocation/VM |
| 3584 / 4096 MiB, 2 CPUs | Adopted RAM; early Android init | Gunyah map error 13, Linux -22 |
| 2048 MiB, 2 CPUs, owned display | Vendor services reached | PAGE_FAULT attempt -22 |
| 2048 MiB, 2 CPUs, native entry/headless | Removes ART and owned display | Same -22 |
| 2048 MiB, 1 CPU, full early bootstrap | Guest zram/setup enabled | Same -22 |

The headless variant is a developer comparison, not an invisible display
fallback for the app. No `BOOT=1`, Android desktop or complete GPU guest session
has been established on this new path. A controller race that could mask child
exit 33 as missing-RSS/resource failure 52 has been corrected in source.

Offline disassembly of the exact host ROM confirms the PAGE_FAULT structure
layout used by the new logger. The host driver reports map error **13** and
translates an unknown hypervisor error to EINVAL. The inspected public Gunyah
enum does not name 13. Do not relabel it NOMEM/10, an alignment error or a proven
resource quota. The earlier Android 16 map-10/ENOMEM observations are separate.
No live protected/donated VMM memory was read or debugged.

## Fragmentation and CPU-only comparisons

MemAvailable includes potentially reclaimable memory; it does not report how
many large physically contiguous free blocks exist. A host snapshot with about
6 GiB MemAvailable had very few free blocks above 64 KiB. The exact counts change
with unrelated host activity. `/proc/buddyinfo` describes the current free pool,
not all movable/cache pages and not a definitive fragmentation percentage.

The saved-metadata tool `tools/fragmentation_report.py` reports each zone's free
orders and per-size splitting upper bounds. It never writes a setting or starts
an allocation. Its size columns overlap and must not be added together.

| VMM / guest kernel | VM / checked workload | Preparation | Observation |
| --- | --- | --- | --- |
| Stock / stock Microdroid 6.6.118 | 512 / 384 MiB | Ordinary backing, no policy | Full write/readback and normal exit |
| Stock / custom guest 6.12.74 | 512 / 384 MiB | Ordinary backing, no policy | Full write/readback and normal exit |
| New source VMM / custom 6.12.74 | 512 / 384 MiB | Sparse adopted memfd, no policy | Full write/readback and normal exit |
| Stock / custom 6.12.74 | 1024 / 768 MiB | Ordinary backing, no policy | Stopped after 384 MiB write progress; no readback completion |
| New source VMM / custom 6.12.74 | 1024 / 768 MiB | Sparse adopted memfd, no policy | Stopped after 448 MiB write progress; attempt -22 |
| New source VMM / custom 6.12.74 | 1024 / 768 MiB | Six-key advised prefault only | 34,496/262,144 THP pages (13.16%); -22 |
| New source VMM / custom 6.12.74 | 1024 / 768 MiB | Owned 4 GiB-budget pressure/release, then advised prefault | 157,264/262,144 THP pages (59.99%); full 805,306,368-byte write/readback, normal exit |
| New source VMM / custom 6.12.74 | 1536 / 1280 MiB | Initial 6 GiB-budget pressure | Floor guard stopped pressure; no VM |
| New source VMM / custom 6.12.74 | 1536 / 1280 MiB | Pressure with next-batch headroom, then advised prefault | 229,116/393,216 THP pages (58.27%); stopped after 1088 MiB write progress, -22 |

The successful pressure helper touched/released 4064 MiB and sampled its own
contents with zero errors. The later headroom-safe run touched/released about
4799 MiB, also with zero sampled errors. Pressure and VM RAM are separate phases;
the later implementation releases pressure before creating guest backing.
Only the helper's own OOM score is changed to make it an early victim. No other
app is manually killed or protected; ordinary Android reclamation is used.

The initial pressure implementation checked the floor before a 64 MiB batch,
which could cross the floor during that batch. The parent correctly stopped it.
The helper now checks the **next batch**, retains execution slack above the same
floor and keeps the independent parent guard. The floor was not lowered.

This is evidence that memory preparation/folio distribution materially affects
this workload. It is not proof of the hypervisor's internal limit, deterministic
recovery, all-THP backing or a fix for arbitrary host fragmentation. The new CPU
probe requires matching nonce, architecture, exact workload size and completion;
stock VMM exit zero alone was observed even after a guest failure and is rejected.

Additional negative gates: a 16 MiB process-local 2 MiB collapse comparison
returned ENOMEM for all eight requests; a medium-only 128/256/512 KiB advice gate
produced no THP pages. Neither negative gate was used to admit a VM variant.
Mapped hole punching remains retired.

The Linux [THP documentation](https://docs.kernel.org/admin-guide/mm/transhuge.html)
describes fallback to smaller pages and reclaim/compaction tradeoffs. The
[DroidVM pool design](https://github.com/Droid-VM/gh-hugepage-reserve/blob/master/POOL_DESIGN.md)
uses a host kernel module/early reservation; it is evidence for comparison, not
an allowed installation path on this device. Global compaction, extra THP sizes,
module loading and host restarts remain unauthorized.

## Source and remaining work

`tools/owned_display_probe/` contains same-signer test instrumentation, the native
producer and bounded developer-controller sources. Generated pins, arguments,
UIDs, copied images and compiled artifacts are private build inputs. These are
not a portable, automatically enabled runtime bundle; do not copy arbitrary
binaries or infer permission to run them from these notes.

The unmodified GLES-only ColorBuffer export lacks AHB backing when Vulkan is
disabled. A separate RGBA8 GLES-backed AHB implementation now passed a no-VM
check: an emulated GLES context clears three random colors, its normal window
flush updates the ColorBuffer, a one-pixel readback checks/waits for completion,
and the exported AHB is actually presented and checked by screenshot. Surface
recreation and the next color also passed. An earlier CPU-upload comparison
passed separately. This retains normal flush and does not revive the old
native-window setter/flush bypass. Shared/exported references are distinct and
consumer FD duplication releases its exported reference on all paths.

Negative prototype checks are retained: the first Java loader namespace lacked
a required process-local library path; a Surface-readiness failure led to registering callbacks before view attachment;
a getGlString query without an emulated context was refused. One failed native
build accidentally ran the earlier upload binary again; that run is excluded
from the new GL-draw qualification. Developer execution now checks a successful
source/binary manifest. None of these failed checks started a VM.

Pinned [source overlays and limitations](../native/owned_display/source-overlays/README.md)
are published; generated device inputs and binary artifacts remain private. Android boot, guest frames, repeated reopen,
network/input and sustained 60/120 Hz still need qualification before app
integration or release.

## Later fragmentation detail and retained negative comparison

A migrate-type snapshot explains why total buddy counts overstate useful large
blocks: Movable had no free 64 KiB-or-larger blocks, while CMA had 66 free 4 MiB
blocks. Unmovable/Reclaimable also had no free 64 KiB blocks. These are transient
counts, not a universal threshold. The metadata tool now accepts optional
pagetypeinfo and separates CMA/HighAtomic/Isolate from ordinary candidate types.
GFP/watermark/pinning rules still prevent this from being an allocation promise.

Read-only host config confirms COMPACTION and MIGRATION are compiled in. Fresh
16 MiB shmem collapse still failed all eight 2 MiB requests after pressure.
A separate anonymous mapping was THP-eligible, retained its contents, but both
2 MiB collapse requests returned ENOMEM. During each anonymous call interval,
compact_stall/compact_fail increased by three and compact_success by zero;
these counters cover the whole host. No global compaction control was written.

Offline inspection finds dedicated trusted/OEM CMA device allocators, not a
general ordinary-memfd import. They are configured/single-lease vendor devices;
only path/permission metadata was read. No FD allocation/ioctl or unrelated VM
operation was attempted. Public long-term-pinning rules also distinguish CMA
from ordinary pinned backing; a large CMA free count does not establish usable
protected-guest RAM.

The subsequent 1536 MiB/one-CPU Android comparison used a new disk clone, low-RAM
guest boot parameter, guest-only 1 GiB zram capacity and explicit pressure before
backing. It prepared 223872/393216 THP pages (56.93%), reached early guest setup,
and stopped with PAGE_FAULT -22; no BOOT=1 or guest frame. Its VMM reported exit
33, while the supervisor's missing-RSS exit-boundary race still reported 52.
The earlier waitpid fix handles one race but not all process teardown ordering;
52 must not be described as proof of exceeding the RAM limit.

An optional developer Surface-rate request now accepts only supported 60/120 Hz
modes at the current resolution and requests a seamless change for its own
Surface. A request is not a measurement of actual presentation. The 120 Hz
performance goal remains unverified.

## Android boot and real guest Surface recovery, 2026-10-21 JST

The 1024 MiB low-RAM comparison prepared **262144/262144 THP pages** before
adoption. Its 128 MiB shared I/O pool is inside that RAM; the guest reports about
855000 KiB usable memory. Guest-only zram capacity is 1 GiB. No Gunyah page-fault
attempt was logged in this bounded session. Android reached BOOT=1, SurfaceFlinger,
SystemUI and Adreno GLES. The initial session still failed display ResourceFlush
and stopped when the broad kernel-log scanner saw vendor `BUG:svprt` text.

Offline analysis of the exact saved ROM identifies both strings as unconditional
printk calls on the normal key_thread_write proc-write path, not a BUG/WARN trap.
The exact-format trace exception is now used only by the exact-host-17 developer
controller. All other BUG text, unknown values/suffixes, Oops/panic/page failures,
unreadable monitor state and health taints still stop it. Default diagnostics
retain the strict original behavior. Tests cover these distinctions.

The transfer failure had a separate cause: legacy non-blob resources had no
exported/cached handle in the new renderer/rutabaga path. The renderer now exports
its real ColorBuffer AHB for that resource type, and Android create_3d caches the
actual duplicated-FD handle. Unsupported resources still have no handle. No
native-window setter, CPU-frame fallback or flush bypass was added. A no-VM test
now exercises resource_create/export through the same public renderer C API,
normal GLES window flush, three actual presented colors and Surface recreation.

A fresh clone using that stack then passed:

- 1024 MiB backing, all sampled pages THP; exact identity/seals adopted.
- Android BOOT=1; Adreno GLES 3.1; QuickstepLauncher actually resumed.
- A host screenshot of the owned GPU layer shows the Android home and icons.
- Recreating its Surface shows the same home; a separate guest screenshot was
  captured for diagnosis, not used as the GPU display source.
- Identity-checked guest shutdown, VMM exit 0 and controller exit 0.
- Same host boot, system_server/zygote/trusted VM PIDs, taint 4608, Enforcing,
  six policy values restored never, and all three original Android source-disk
  hashes unchanged.

The parent Surface accepted a supported seamless **120 Hz request**, with the
host display reporting about 120.00002 Hz at request time. Neither that fact nor
three screenshots establishes sustained 120 presented frames per second. Buffer
reuse/acquire/release ordering, longer runs, guest refresh configuration and
actual presentation timing remain follow-up work. One clean 1 GiB session is
not 4 GiB qualification or a ready general-user APK.


## One-shot host compaction, 2026-10-21 JST

The first explicit 120 Hz guest-display comparison failed before BOOT=1 and
before the motion workload was installed. Although its 16 MiB handoff gate
passed with 2656/4096 THP pages, the actual 1 GiB preparation produced only
111876/262144 THP pages (42.68%). The later batches fell back to base pages.
Gunyah stopped at attempt -22; VMM33 and controller33 were retained. This is
not a 120 fps result, nor proof that refresh rate caused the fault. Completed
postflight preserved the host boundary, all six never values and source disks.

The user then explicitly permitted host runtime memory compaction provided
host kernel files and partitions were not modified. A boot-bound root helper
held the common owner/maintenance leases, checked the exact platform, healthy
taint, Enforcing, six original policies and the 1536 MiB available floor. It
consumed a durable O_EXCL receipt and issued exactly one write of `1\n` to
`/proc/sys/vm/compact_memory`, supervised through its known child with a
45-second deadline. No VM or owned pressure ran concurrently. No persistent
tuning, cache drop, host kernel/module/partition change or reboot was used.

The write returned two bytes, errno zero, in **18 ms**. Snapshot comparisons:

| Observation | Before | After |
| --- | ---: | ---: |
| MemAvailable, KiB | 4934608 | 4931156 |
| Ordinary Movable/Reclaimable/Unmovable free bytes in blocks >=64 KiB | 0 | 0 |
| CMA free KiB in blocks >=64 KiB | 0 | 6336 |
| compact_migrate_scanned | 74796414 | 74801182 |
| compact_free_scanned | 176489176 | 176901139 |
| compact_isolated | 11106838 | 11111838 |

These global snapshots include concurrent Android activity. They establish
that the request completed and scanning counters advanced, not how many useful
ordinary pages became available or which process caused every counter change.
The ordinary >=64 KiB free count did not improve; recovered CMA is not normal
long-term-pinnable VM backing. compact_success/fail did not change in this
interval; those counters alone do not classify this explicit sysctl attempt.

A separate post-compaction 16 MiB handoff observed **4096/4096 THP pages** and
512 compound heads, and passed identity/seals/offsets/full-content checks plus
unchanged observed PFNs. Both restorers succeeded. Host boot/core/trusted-VM
identities, taint4608, Enforcing, all six never values and original three disk
hashes were retained. Natural allocation variability prevents attributing the
16 MiB improvement to compaction alone. Large Android RAM is not qualified by
this result. The single-use receipt remains consumed; no repeat is scheduled.

The new developer-only `PLB_PREALLOCATED_REQUIRE_ALL_THP` gate conservatively
requires the backing condition from the clean 1 GiB GPU session. It checks
every prepared batch and all RAM again immediately before handoff. Missing,
unreadable or base-page backing refuses before VMM exec. This does not pin the
pages or guarantee later Gunyah/GPU stability. It is not enabled in the APK or
the CPU comparison by default.

## Guest 120 Hz and actual presentation measurements

With the conservative all-THP gate, a later 1 GiB session passed the per-batch
and final complete-backing observations, BOOT=1, Adreno GLES and Surface
recreation. The guest display reported about 120.00001 Hz and an 8333333 ns period.
A separate permission-free guest GLES motion APK was installed through the fixed,
identity/hash-checked guest-only client and removed after observation.

Samsung's SurfaceFlinger `--list` wrapped names in `RequestedLayerState{...}`.
The initial collector wrongly expected a plain name. Its assertion failed and
removed the motion APK, while the VM instrumentation itself later passed normal
guest shutdown, VMM0 and controller0. A short independent collection during the
same session used the actual `PLB owned display#...` debug name, relaunched the
matching guest-only workload, and successfully removed it afterward.

The six overlapping 127-frame histories yielded **170 new actual-present times
in 1.433333385 s: 118.6046 fps**. Median interval was 8.333333 ms, p95 about 8.333386 ms and
longest 16.666615 ms. Each adjacent history overlapped, so the collector did not
skip a whole ring. This is a short bounded result, not sustained 120 fps or input
latency. It counts unique finite second-column actual-present timestamps, not
render callbacks, guest requested refresh or duplicate timestamps. The column
semantics are defined by AOSP
[FrameTracker::dumpStats](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/services/surfaceflinger/FrameTracker.cpp).
`tools/presentation_report.py` rejects malformed/reset histories and withholds a
complete FPS result when adjacent histories do not overlap. Tests distinguish
unsignaled/zero fences, duplicate presentation times and lost history.

A longer comparison retained 48 overlapping histories over 21.82 s, but is **not a
valid continuous-motion performance benchmark**: the developer observer retried
HOME whenever its O_EXCL evidence PNG already existed, interrupting the workload
repeatedly. The overall mixed-content layer rate was 41.61 fps and must not be
attributed to the motion workload. The observer now attempts GUI preparation
and evidence capture once per owned session, regardless of an existing file.
That fix is compiled; a clean longer device benchmark remains pending.

This longer session reached normal guest shutdown/VMM 0, while the controller
reported 52 due to the retained teardown/RSS observation race. Keep these exit
codes separate. Completed postflight retained the host boundary, six never
values and original source disks. No extra compaction was performed. Product
Android readiness remains false; asynchronous acquire/release ordering and
product runtime/input/network integration are still open.

## Further headless compaction comparisons

The user explicitly requested further tests without GUI. Two additional,
separately receipted compact_memory writes were bounded to45s each. The existing
spent receipt was retained. No persistent/proactive/extra-THP/cache-drop setting
was changed. The prior operation already covered all zones; this comparison
changes preparation/observation, not the set of host zones.

| Comparison | Write duration | Ordinary Movable free KiB in >=64 KiB blocks | VM outcome |
| --- | ---: | ---: | --- |
| A: compaction without owned pressure | 69 ms | 0 before/after | 1024 MiB backing only20024/262144 THP pages; stopped at448 MiB write, attempt-22; supervisor88 |
| B: owned pressure fully released, then compaction |1190 ms |30848→1044224| 1024 MiB/768 MiB full nonce-bound readback passed, all262144 pages THP, controller0 |
| B follow-up, no additional compaction | — | not a simultaneous free-list snapshot |1536 MiB/1280 MiB full readback passed;305560/393216 THP pages;controller0 |
| B larger follow-up, no additional compaction | — | not a simultaneous free-list snapshot |2048 MiB/1792 MiB stopped after1280 MiB write;287448/524288 THP pages;attempt-22;controller90 correctly rejected missing readback despite raw entry exit0 |

Owned pressure released4286 MiB with zero sampled pattern errors before B;
its allocation and the VM did not overlap. At B's immediate snapshots, total
ordinary candidate free KiB in >=64 KiB blocks rose45568→1078912, while CMA was
reported separately. Concurrent Android activity still prevents attributing
all changes to the single write. The observed larger-block recovery and full
VM readbacks establish a useful bounded improvement, not guaranteed4GiB Android
or long-term memory stability. The current verified CPU comparison is1536 MiB
configured RAM with1280 MiB of fully checked workload; not every configured byte.

Both round-level16MiB handoffs retained all contents and all4096 observed PFNs,
with useful THP and both restorers. The B-specific handoff was collected after
the first B VM, before the1280MiB follow-up: an offline collector initially
rejected clipped low-order pagetypeinfo counts, while its shell continued into
the already prepared first B probe. That dependency bug was corrected with
checked sequential orchestration. The earlier A handoff had passed before the
round's VMs. Do not claim the later B handoff preceded its first VM.

For B's saved metadata, only low-order counts were clipped as `>100000`.
Their exact values remain unknown. The >=64KiB counts quoted above use only
independently exact higher-order columns; no clipped count is treated as exact.
Original raw snapshots and failed collectors remain private. No further
compaction write is authorized by these consumed round receipts.

All completed postflights retained host boot/core/trusted-VM identities,
taint4608, Enforcing and all six never policies. A later headless Android17
session also retained all three original source-disk hashes. Detailed peripheral
work and its limits are [recorded separately](android17-peripherals.md).
