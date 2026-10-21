# Android 17 app GUI validation — alpha.7

2026-10-21; SM-S948Q / S948QOPU1BZI9, selected Tail ADB transport.
This record concerns the actual app viewer, separately from the alpha.6
headless boot/190-second Internet tests and earlier developer display tests.

## Fixed during real app operation

Opening Bridge starts an idle Debian manager. It retained the shared VM owner
lease and prevented the Android installer/check from acquiring it. Android
startup now asks the serialized manager to release only an idle runtime before
installation/check. An active VM, proxy, terminal session, renderer, GPU server
or worker refuses the handoff. Root/module authorization and delivered reply
are required. There is no forced-stop fallback. Automatic runtime replacement
also uses idle-only stopping, preserving a live Debian session after APK updates.
Old managers without this protocol require an explicit normal stop.

The updated native helper's idle release was observed on device; the final app
path proceeded beyond the installer without the previous BUSY result. The
real native socket fixture checks authorization, every active role and a
client disconnect. These tests are not a live-user-session interruption test.

Pre-execution `PLB_PREALLOCATED_THP_REFUSED vmm_started=0` now selects the memory
preparation explanation rather than a misleading root-denied error. Code 52
uses the general preparation error. Unsupported512MiB advice was removed.
Unlock/back/home controls are disabled before guest readiness and after failure;
Close remains available. These states and return to Main were observed on the
physical UI. The Debian status reflects the released runtime.

## Memory result and limits

Before the separately approved compaction, app-path attempts safely refused
backing checks at96, 368 and 192 MiB (a repeated 192 MiB attempt also refused).
All stopped before VMM execution. One parent teardown returned52; the exact
preallocation marker preserves the correct UI diagnosis without weakening the
memory guard.

The [fourth single-use compaction comparison](android17-gui-compaction-proposal.md)
completed in 566 ms and passed the 16 MiB full-content THP handoff. The following
single GUI attempt admitted400 MiB but refused incomplete evidence in the next
batch. No Android guest started in this GUI round. Screen/touch, Surface
hide/show, in-viewer Internet reconnect and normal running-VM viewer close
remain **unverified**. Headless alpha.6 Internet success is not GUI qualification.

No extra write/retry followed the consumed compaction approval. The original
boot/core/trusted VM, taint 4608, Enforcing SELinux, all six never policies and
three original Android source disk hashes matched on final postflight. No
owned VM/network child remained. No host restart, system/kernel/partition/
protection changes or physical microphone/camera/LED test was performed.
Raw logs, screenshots, boot identifiers and receipts stay private.

## Android OS cache-release notification comparison

The user next requested an OS-level memory release. The device advertises
`am send-trim-memory`; ten freshly checked user-0 cached third-party processes
received `BACKGROUND` (40). Core, foreground/service, keyboard, root, VPN,
Tail ADB and VM/Bridge processes were excluded. Each process identity/state
was checked again immediately before the command. All ten commands were
accepted; this does not prove callback execution in frozen apps or a particular
amount of memory reclaimed. No kill/force-stop, cache deletion, drop_caches,
extra compaction or global policy change was used.

Observed MemAvailable changed from 5,369,568 to 5,815,176 KiB (+435 MiB), while
other host activity continued. This is a correlation, not an isolated effect.
A subsequent check first encountered the idle Debian owner lease after the APK
update; the supported idle-only release returned its exact acknowledgement.
The following 16 MiB handoff preserved every byte/PFN but showed only
3,724/4,096 THP pages. The test stopped at that stricter manual admission check;
no additional GUI VM attempt followed. Host boot/core/trusted VM and health
remained unchanged and all six policies remained `never`.

The standard callback encourages applications to release rebuildable resources;
it does not guarantee contiguous physical pages. See the official
[ComponentCallbacks2 contract](https://developer.android.com/reference/android/content/ComponentCallbacks2)
and [ActivityManager shell command](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/services/core/java/com/android/server/am/ActivityManagerShellCommand.java).
`COMPLETE` and the running-pressure levels are deprecated; this comparison used
`BACKGROUND`, which is still documented. No new automatic system-wide memory
notification function is enabled in the app.

## Build verification

The final signed release APK passed 81 JVM tests and release Lint (zero errors,
45 warnings). The host/native suite passed 235 cases, with three Linux-only
cases skipped on macOS. This means 232 cases executed there, not 235 executed.
The final installed APK hash matches the signed release artifact. Android 16
frozen references remain unchanged.
