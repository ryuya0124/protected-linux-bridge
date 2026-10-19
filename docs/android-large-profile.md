# Guarded Android 17 / 1.5 GiB app profile

## Current preview.6 safety update

The larger follow-up found host `Bad page cache` warnings in the mapped-hole-punch
experiment. Preview.6 removes that operation from the production library and
retires the diagnostic helper. The app now uses prefault-only admission and
refuses large starts if the host has BAD_PAGE/OOPS taint. The current phone refuses
this mode; no further load is run in this host session. The changed mode is not
boot-validated on a clean session. The successful preview.5 sessions below are
historical, not evidence for the new path. [Investigation and measured limits](memory-increase-2026-10-19.md).

## Historical preview.5 integration

Status: 2026-10-19, 0.4.0-preview.5. Native runtime validation passed;
the phone remains locked, so the app selection/viewer screen is not yet verified. The prior
standalone 1536 MiB diagnostic reached Android boot completion and Launcher3 home
in two bounded runs. This is not evidence of 4 GiB guest-resident memory or of
long-term Android stability.

## Implementation

- `AndroidGuestRuntime`: the 1536 MiB selection requires a per-launch acknowledgement;
  install only the bundled native manager and backing helper into private runtime
  storage. The non-exported viewer defaults to 512 MiB when opened without extras.
- `native/android_large_profile.h`: verify the exact SM-S948Q / Android 16 build,
  host kernel release and installed crosvm SHA256. Copy the pinned VMM byte-for-byte
  into app-private storage. Copy offline Android disks through no-follow files,
  compare copied bytes, fsync and promote a completed new profile. An interrupted
  preparation is recovered only from known private filenames; unknown files cause
  refusal. The old writable profile and Debian disks are never attached here.
- `native/android_owned_process.h`: before launching the guarded profile, move
  only the root controller itself out of its launching app's cached-app freezer
  cgroup into the existing unified root cgroup. Verify membership afterwards; an
  unexpected layout refuses startup. Children inherit that placement so freezing
  the viewer cannot freeze the restoration guardian. No freezer policy, another
  process's membership, cgroup directory or CPU policy is changed.
- `native/android_memory_prepare.h`: a single owned allocation worker prepares
  ordinary RAM before the VM or host policy change. Budget is 6112 MiB, floor is
  1536 MiB MemAvailable, worker alarm is 25 seconds and supervisor limit is 30
  seconds. Its own OOM score is 1000. Closing the authenticated viewer cancels
  preparation. No other process is signaled or has its OOM protection modified.
- `native/small_shmem_guard.h`: three fixed sysfs nodes, initial `never` required,
  filesystem/type/ownership checks, write/readback verification, ten-second normal
  restoration and independent forty-second guardian. The guardian inherits the
  common owner lock and terminates only its original controller on deadline.
  Unexpected third-party policy changes are reported rather than overwritten.
  Host-wide policy is disclosed before selecting the mode; no persistent opt-in
  or silent boot-time switch exists.
- `tools/memory_backing_probe.c`, production build variant: retains the original
  shared memfd and offsets, with 64 MiB base-page margins. It prefaults/refaults
  only its own backing, checks complete middle-range THP flags before donation
  and refuses with exit 89 on incomplete coverage. Contiguous base pages alone
  never satisfy this gate. Diagnostic ioctl observation is excluded from the APK.
- `native/android_guest_manager.c`: serialize disk work, allocation and VM lifetime
  through the same owner lock. Fork allocation/guardian/VMM before display threads.
  The fixed software-GPU/three-disk/vsock profile remains protected; guest-only
  bootstrap, frame transport and orderly shutdown use the verified guest identity.

The backing flag snapshot is not a physical page pin, hypervisor resource
measurement or proof of future stability. RAM reclamation may affect other apps,
and a kernel/hypervisor hang may prevent the guardian from running. No boot,
host kernel, SELinux, global mount or protection changes are performed.

## Device observations

Two bounded sessions exercised the signed APK's native runtime through the
same app-UID-checked socket used by the viewer. Ordinary preparation touched
6080 MiB and 6016 MiB with zero sampled readback errors. Each session observed
352256/352256 middle pages present with THP flags and zero flag-read errors.
Eight valid PNG frames were returned in each; the first final image was visually
verified as the Android Launcher3 home screen. Guest wake/HOME commands were
accepted, and closing the socket requested orderly guest poweroff. The manager's
124 result denotes its bounded session/teardown path; crosvm separately reported
successful guest shutdown. No vCPU fault was logged in these sessions.

An early client disconnect cancelled preparation with exit 40 before any
`advise` change or VMM launch. All three modes stayed `never`. The original 512 MiB
writable disks matched their preceding hashes throughout larger-profile tests;
that does not mean a later deliberate 512 MiB boot leaves its own data unchanged.
Host boot identity, main system processes, SELinux Enforcing and the unrelated
trusted VM remained unchanged. Raw console traces, PFNs, identifiers and private
images are excluded from Git. [Sanitized app results](data/android-large-profile-2026-10-19.json).

The locked-device app-launch attempts do not establish the visible selector or
ImageView interaction. A foreground UI check is still needed after unlocking. The rebuilt 512 MiB
fallback also completed setup, returned eight valid PNG frames and shut down
through the guest endpoint without policy changes. A usable 512 MiB home screen
was not established; its existing severe memory-pressure limitation remains.
These short native sessions do not establish long-term stability.

## Validation and fallback

Python/native checks cover guardian normal exit, controller death, deadline,
unexpected original mode and concurrent policy changes; backing summaries reject
missing pages, hidden PFNs, unreadable flags and ordinary contiguous base pages.
The 121 Python/native checks, 27 JVM tests, signed release build and release
Lint passed. Root preflight additionally requires the exact profile marker; a
wrapper denial with exit zero is rejected. Signed APKs retain the
previous release certificate; installation is an in-place update, not uninstall.

The original 512 MiB configuration remains selectable and uses no host THP
policy change or large allocation preparation. Its older writable data is
independent of the new profile. Closing the viewer stops only the tracked guest;
Debian can be opened afterwards. An OS/crosvm update disables 1.5 GiB until its
compatibility pins are reviewed. Never weaken the pins merely to make it launch.

See [display preparation and limits](android-guest-display.md),
[historical aggregate results](data/small-shmem-trials-2026-10-19.json), and
[host boundary](host-boundary.md). Networking, audio, hardware GPU and physical USB
are not established for this Android preset.
