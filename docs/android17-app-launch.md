# Android 17 experimental app launch

The Android 17 branch now exposes a deliberate launch action for the separately
qualified 1 GiB GLES configuration. This is an experimental integration, not a
claim of general Android VM compatibility or production stability. Android 16's
frozen branch/release is unchanged.

## Use

Install the signed `0.5.0-alpha.7` experimental APK and grant Bridge root. Open
**VM → Android guest → Open Android guest** and read the per-launch conditions.
The only enabled preset is 1024 MiB RAM, one vCPU, 480×800 direct GLES display,
with a requested 120 Hz virtual display. Actual sustained 120 fps is unqualified.
The app session has no elapsed-time limit. It ends on viewer disconnect, guest
exit, crash or a monitored failure. Boot, cleanup and host-policy restoration
still have bounded deadlines. There is no automatic startup or retry. Closing the viewer disconnects its input
channel and requests shutdown of this owned guest. Existing Debian sessions must
be closed first; the launch code refuses to stop them implicitly. An idle Debian
startup helper is released automatically through its authenticated idle-only API.

The app installs its bundled, compiled-and-hashed runtime in a **new** private
`files/android17-app-runtime` directory. The earlier developer runtime remains
available for diagnosis. The private `android-guests/h17mfix` profile must already
exist, with the pinned Android 17 ramdisk and separate working/misc/FRP disks.
This preview does not redistribute Android OS disks and cannot prepare a fresh
Android installation just from the APK. Missing runtime/profile files refuse
startup with an error. Do not remove or recreate an existing profile to suppress
an error; retain backups of its mutable disks.

Physical speaker/microphone/camera/flash HAL forwarding and guest Vulkan are
**not enabled by this launch path**. Debian's audio and LED settings
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

## Earlier alpha.5 integration validation (2026-10-21)

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

## Internet and session lifetime (alpha.6)

The launch confirmation has an Internet switch, enabled by default and remembered
for the next launch. The viewer's **Reconnect guest network** button restarts only
the host relay, retaining a healthy guest TUN and Android network registration.
It does not restart Android or the VM. Closing the viewer stops
that guest and its relay. A network error is shown separately from VM boot state.

The guest creates its own `plb0` TUN and registers an ordinary Android network.
Android performs its real connectivity validation; Bridge never forges VALIDATED.
DNS and TCP/UDP travel through an authenticated-by-CID vsock connection to a host
relay running as Bridge's app UID. Ordinary guest apps need no proxy configuration.
The relay rejects host-local, loopback and link-local destinations and closes
inherited VM/disk/lock descriptors before dropping privilege. This does not create
a host VPN, change host routes/DNS/firewall rules or enable global IP forwarding.

This is IPv4 outbound TCP/UDP support through a userspace tunnel, not full virtual
Ethernet, inbound forwarding, raw ICMP, multicast or complete IPv6 support. Host
connectivity and remote services can still fail. Reconnects reuse only files whose
full hashes and private directory ownership match the compiled payloads.

The app VM, network agent and relays have no 180-second/30-minute usage limit.
Per-flow idle/connect deadlines, health checks, bounded startup/cleanup and the
independent 40-second host THP-policy restorer remain. Their purpose is to bound
a failed operation rather than expire a healthy running VM.

### Failure handling and validation

The 0.5.0-alpha.6 build passes 81 JVM tests and release Lint (zero errors,
45 warnings). The host Python/native suite contains234 cases:231 execute on
macOS and three Linux-only LED cases are skipped there. Added tests cover
unlimited relays, disconnect cleanup and a ready network controller dying later.

During integration, three independent preparations refused before VMM execution
when a backing batch failed all-THP admission. Later sessions reached Android
boot and real three-site HTTPS with Android reporting `plb0` as the validated
default network. Early network-only restart attempts failed even though initial
Internet access passed. Those sessions still shut down with VMM0/controller0;
these negatives are retained separately from later results.

Reconnect now verifies the cached payloads instead of truncating and uploading
them again, permits loopback TCP listener reuse after disconnect, and bounds the
wait for removal of the previous owned guest NetworkAgent. It never takes over
a different active guest network. The fixed diagnostic APK is used only for
non-GUI tests and is removed after validation.

A later app-path session passed three-site HTTPS before and after a190-second
post-boot hold. Android reported its own `VALIDATED` default `plb0` network on
both checks. This confirms operation past the removed180-second limit, not
long-term reliability or a30-minute endurance result. Its subsequent network-only
restart still failed in the startup script; that negative remains in the record.
The owner requested guest shutdown and both VMM/controller exited0.

A second190-second session again retained working HTTPS, but replacing the whole
guest network left fresh HTTP connections failing despite Android still showing
validation. The reconnect design now preserves a hash- and PID/birth-verified
healthy guest tunnel/NetworkAgent and replaces only the unprivileged host relay.
Normal VM closure/crash still requests full owned guest-network cleanup. The
old controller transfers its guest-network responsibility only while the same
VM manager retains the owner lease; a new controller verifies guest health before
adoption. A missing/dead tunnel remains a setup error or needs cold setup.

Cold-cache testing also exposed readiness files left by a previous guest power-off.
The next boot must not accept those files or a launcher PID as evidence of a
live network. Cold startup now removes stale readiness/log receipts before
launch and checks the actual supervisor's current guest boot and process birth.
Verified adoption is a separate path and never clears a healthy live session.

The first cached cold-start comparison also exposed a premature setup receipt:
the NetworkAgent was registered but had not become Android's active default.
Readiness now observes the exact registered network selected as default with
interface `plb0`, using the guest main looper. It still does not assert or forge
Internet validation. The bounded setup deadline is separate from VM usage time.

The corrected alpha.6 app session completed the full network test: exact Android
boot; three certificate-checked HTTPS targets; another successful check after
a10-second hold; successful host-relay reconnect while retaining the healthy
guest network; all three HTTPS targets again; guest shutdown; VMM0/controller0.
Android reported the active `plb0` network as genuinely validated each time.
The positive headless instrumentation completion receipt was recorded.
The final cold-cache repeat also completed a190-second post-boot hold, then
passed the same three HTTPS targets after host-relay reconnect. Both final
sessions ended with VMM0/controller0 and positive instrumentation receipts.

Final postflight retained host boot/core/trusted-VM identities, taint4608,
Enforcing, all six `never` settings and all three original source-disk hashes.
No owned VM/controller/relay remained. The diagnostic APK and shell temporary
folders were removed after preserving their verified logs in app-private storage.
The power-off-first test leaves an ENODEV guest-network cleanup query and the
known vCPU closed-channel shutdown warning; these are retained with the normal
owned process exit results. They are not a physical viewer-close test.

Machine-readable results: [alpha.6 validation](../compatibility/android17-network-validation.json).

## Alpha.7 actual viewer checks

Idle Debian startup support no longer blocks Android installation/checks. Active
Debian sessions are refused rather than terminated. Before guest readiness and
after a failure, guest input buttons stay disabled; Close remains available.
Actual pre-execution THP failures now show the memory preparation explanation.

The [GUI validation record](android17-gui-validation.md) retains the failed
backing checks, separately approved one-shot compaction and cache-release
notification comparison. No actual guest frame or touch result was obtained in
this round. No automatic compaction or host-wide cache-release feature was added.
