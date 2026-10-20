# Android 17 experimental app launch

The Android 17 branch now exposes a deliberate launch action for the separately
qualified 1 GiB GLES configuration. This is an experimental integration, not a
claim of general Android VM compatibility or production stability. Android 16's
frozen branch/release is unchanged.

## Use

Install the signed `0.5.0-alpha.5` experimental APK and grant Bridge root. Open
**VM → Android guest → Open Android guest** and read the per-launch conditions.
The only enabled preset is 1024 MiB RAM, one vCPU, 480×800 direct GLES display,
with a requested 120 Hz virtual display. Actual sustained 120 fps is unqualified.
The session has a 3-minute supervisor limit while integration is being tested.
There is no automatic startup or retry. Closing the viewer disconnects its input
channel and requests shutdown of this owned guest. Existing Debian sessions must
be closed first; the launch code refuses to stop them implicitly.

The app installs its bundled, compiled-and-hashed runtime in a **new** private
`files/android17-app-runtime` directory. The earlier developer runtime remains
available for diagnosis. The private `android-guests/h17mfix` profile must already
exist, with the pinned Android 17 ramdisk and separate working/misc/FRP disks.
This preview does not redistribute Android OS disks and cannot prepare a fresh
Android installation just from the APK. Missing runtime/profile files refuse
startup with an error. Do not remove or recreate an existing profile to suppress
an error; retain backups of its mutable disks.

Internet, physical speaker/microphone/camera/flash HAL forwarding and guest
Vulkan are **not enabled by this launch path**. Debian's audio and LED settings
must not be interpreted as Android guest support. PNG/raw capture is not used as
a fallback; a failed owned display stays an error rather than silently reading
back frames through the CPU.

## Boundaries

- Exact SM-S948Q / BZI9 / SDK37 / kernel / Terminal pins and healthy host checks.
- Separate, source-built crosvm and baseline gfxstream DSOs. Old Android 16 and
  crash-associated Vulkan managers retain their execution refusals.
- Per-session random display capability through a private inherited pipe and
  the app's Binder provider. No global virtualization display registration.
- App UID comes from the installed user-0 app directory, not a developer UID
  compiled into an APK. The input socket checks SO_PEERCRED and exposes only
  bounded multitouch packets and allowlisted guest keys/unlock operations.
- Common VM/maintenance locks; only tracked owned children can be signalled.
- Explicit host-wide six-key 16–512 KiB shmem THP acknowledgement. Independent
  restoration guardian remains bounded to 40 seconds. No persistent tuning,
  compaction, cache drop, extra THP nodes or protection changes are added.
- A 16 MiB full-content memfd handoff must show actual THP before VM preparation.
  Every final page of the 1 GiB VM backing must pass the existing all-THP gate.
  A failed scan is a refusal, not permission to retry or relax memory checks.
- Combined diagnostic cap 6 GiB; MemAvailable floor 1536 MiB retained.
- No host kernel, module, boot/system partition, SELinux policy or reboot changes.

The current controller lives in `tools/owned_display_probe/vm_controller.c` with
an explicit app-session build branch. The default developer branch is unchanged.
`native/android17_app_session.h` supplies the narrow interactive protocol and
`native/android17_runtime_install.c` installs only compiled pinned assets.
`tools/build_android17_app_runtime.py` builds the optional bundle from the exact
cached baseline DSOs/kernel; it never starts a VM. A clean source build without
these optional inputs remains buildable and reports the missing bundle at use.

See [runtime history](android17-runtime-validation.md),
[source overlays](../native/owned_display/source-overlays/README.md), and
[host boundaries](host-boundary.md). New validation results are recorded below
separately from the earlier developer rendering results.

## Integration validation (2026-10-21)

- Signed release APK compiles, 81 JVM tests pass and Lint reports no errors.
  Host Python/native suite: 231 cases, 228 run on macOS and three Linux-only
  cases skipped there. Those three real UNIX LED broker cases pass separately
  in an isolated Linux container with synthetic replies and no network/hardware.
- Tail ADB inventory: two built-in input ports, three OS microphone descriptors,
  two advertised flash sources. No AudioRecord, physical camera, speaker or LED
  was activated. These counts do not establish physical capsule independence.
- A waiting Debian manager (no guest child) first held the owner lock. Startup
  refused; its normal stop API returned0 before the next test. No active Debian
  session was killed or disk edited.
- First app-path load: real 16 MiB handoff passed. At the160 MiB backing batch,
  only680/4096 pages were THP; the VMM was not executed. Controller88 and both
  policy restoration receipts were retained. Do not relabel this as a VM crash.
- Second app-path load: all262144/262144 backing pages passed and1 GiB was adopted.
  The test exposed a premature readiness receipt: root adbd/bootstrap existed,
  but `sys.boot_completed` was empty. Inventory ran too early and guest shutdown
  timed out. This is **not** a completed Android boot or a clean shutdown result.
  Readiness now requires the exact `1\n` boot property before enabling input.

Physical viewer operation, multitouch on the new integrated path and LED-on/
recording tests are not claimed. Earlier developer Surface recreation/GPU results
remain separate from these non-GUI app-launch checks.

The corrected third app-path test reached exact `BOOT=1`, Android framework and
HAL inventory, then requested guest shutdown. Both
`ANDROID17_OWNED_VMM_EXIT 0` and `ANDROID17_DEVELOPER_SESSION_EXIT 0` were observed,
with the positive headless instrumentation completion receipt. The VMM retained
a closed-channel vCPU-control warning during normal teardown; preserve that
warning separately from its exit0. All262144 backing pages passed THP admission.

Final postflight retained the original host boot/core/trusted VM identities,
taint4608, SELinux Enforcing, all six `never` policies and all three original
Android source-disk hashes. No owned VMM/controller remained. The signed probe
APK was removed; the signed release app and its private profiles remain.
