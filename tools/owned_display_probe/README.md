# Owned display developer probes

These sources are absent from the released application. They exercise a
separate app-private Android 17 VMM and renderer through the authenticated,
per-session display connection. They do not use the system-wide display slot.

`DisplayProbeInstrumentation` supports a bounded no-VM color test, a source
renderer test and the pinned Android developer session. The host Surface can
request a supported 60 or 120 Hz mode; that request is not a presented-fps
measurement. Surface recreation and teardown are part of the checks.

`guest/MotionActivity.java` is an independently signed, permission-free,
guest-only GLES workload. It automatically finishes after 45 seconds. Its
render counter is deliberately named `rendered`; use SurfaceFlinger actual
presentation timestamps to measure frames shown. The fixed guest-only client
`motion_guest.c` checks the exact guest identity and the generated signed APK
hash. It refuses an existing package and removes only its own matching package.
No general-purpose host or guest command interface is exposed.

The controllers require private generated platform/artifact contracts, owner
leases, memory and health guards. These files are not a portable installer.
Do not reconstruct missing private inputs from guessed paths or hashes. Keep
old crash/Vulkan runners disabled and production guest readiness false until
all integration checks pass. See the [runtime record](../../docs/android17-runtime-validation.md)
and [source overlays](../../native/owned_display/source-overlays/README.md).
