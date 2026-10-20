# Android 17 runtime recovery: device evidence

2026-10-20, follow-up to the offline port and owned-display design. This is a
**developer validation record**, not a working Android guest release. The APK's
`ANDROID_GUEST_READY` gate remains false. Android 16 is frozen.

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
