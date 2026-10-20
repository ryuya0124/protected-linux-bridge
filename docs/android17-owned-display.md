# Android 17: owned display connection

Status: implemented transport, offline builds/tests and device linkage/reflection
checks passed. **No complete new owned-display VMM build, Android guest boot, device display
Binder exchange or GPU frame has been verified.** Separate CPU-only probes are
recorded below.
The Android guest gate remains disabled. These components cannot make the old
Android 16 runtime compatible with the new host.

## Replaced connection

The reference Android display client registers/fetches a global display-service
slot in the system virtualization service. The Bridge's replacement never calls
those APIs. It provides a separate protocol,
`net.ryuya_dev.protectedlinux.display.IOwnedDisplay`, in its private backend.

1. A viewer creates a 256-bit random session and its own serial surface worker.
2. The future qualified root launcher receives the session and viewer UID in a
   fixed 40-byte private stdin pipe. Neither is a command-line argument, property
   or log entry. The launcher is not enabled in this build.
3. That process claims the viewer's lease through the app's existing non-exported
   `net.ryuya_dev.protectedlinux.gpu-surface` provider. Calls must originate from
   UID 0, the claiming PID and the matching session.
4. `RootOwnedDisplayPublisher.initialize()` verifies the exact Android 17 host
   and root-owned backend's digest/mode before loading it. It does not load a
   VMM, start an Activity, start a VM or modify a host service.
5. During private VMM display initialization, the backend publishes its Binder
   to that same provider. The app verifies descriptor/version and links death
   notification before sending its surfaces.
6. Incoming native Binder operations require the configured viewer UID, session
   and a fresh positive surface generation. Root UID alone cannot attach a
   surface to the native endpoint. A detach seals its generation so a delayed
   attach cannot resurrect it. Main/cursor generations are independent.
7. Viewer close or publisher death ends the lease. It does not discover another
   display, retry a VM, reclaim an old lease or clear a system display slot.

The capability guards against stale/wrong sessions and other app UIDs; it is
not a security boundary against malicious root. Root remains trusted. The
current implementation permits one owned display context per root process and
one active viewer per Bridge process. A new viewer needs a new owned launch;
a replacement SurfaceView surface in the same viewer gets a new generation.

`AndroidGuestGpuSurface` now uses SurfaceView. The reference
[`ASurfaceControl_createFromWindow`](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/android17-release/native/android/surface_control.cpp)
requires a parent SurfaceControl handle and returns null without it; the old
SurfaceTexture producer has no such parent layer. Destroyed surfaces are detached
rather than retaining an abandoned producer queue. The worker owns a separate Surface reference until
it is parceled/replaced. Reconnection replays the latest available surface.
The native side retains the most recent imported AHB for an attach replacement.
A viewer process death/VM termination watchdog still belongs to the future
qualified VM launcher; no new process-killing mechanism is introduced here.

## Native presentation and ABI

`backend.h` follows the pinned Rust FFI in
[`gpu_display_android.rs`](https://android.googlesource.com/platform/external/crosvm/+/c7dd8d6950912fa5e6fde7ec2d1616e0ddc090f8/gpu_display/src/gpu_display_android.rs),
including the cursor position function and AHB FD/metadata layout. The adjacent
reference C++ header has older signatures and is not the ABI authority.

The replacement duplicates an exported native handle through the real
`AHardwareBuffer_createFromHandle` CLONE entry, retains the AHB, and submits it
through SurfaceControl. It bounds metadata, dimensions and FD counts, validates
FDs, rejects reserved descriptor bits and protected-content buffers, and never
strips protection flags. Missing real import/Binder entry points fail closed.
Borrowed descriptor FDs are not closed. Fixed per-context surface handles match
the pinned Rust backend, whose `AndroidSurface` does not call the destroy hook.

The NDK-generated Binder implementation and statically linked C++ runtime are
hidden. Only the nine C entry points and JNI configuration function are exported.
This avoids interposing a VMM's separate C++ runtime. Dependencies are ordinary
Android Binder/native-window/SurfaceControl libraries; the backend has no link
to the system virtualization-service AIDL libraries.

CPU framebuffer acquisition returns false; there is no silent PNG/readback
fallback. The old native-window renderer setup and resource-flush bypass are
not carried over. The eight-byte/word ABI, private AHB import behavior,
SurfaceControl presentation, viewer recreation and renderer resource ownership
still require device validation. Main and cursor protocol channels exist, but
the current SurfaceView viewer binds only the main channel.

**Synchronization is not solved by the connection replacement.** Like the
reference route, this first implementation submits with acquire fence `-1`.
Explicit producer completion and release-fence behavior need a separate review
and device measurements before claiming tear-free presentation, zero-copy,
60 fps or a fix for the earlier APK-transfer corruption. Do not enable Vulkan.

## Build and private VMM overlay

Build the bounded backend and root publisher with SDK 37, build-tools 36.1.0,
NDK 28.2.13676358 and JDK 21:

```sh
python3 tools/build_owned_display.py --sdk /path/to/sdk \
    --output artifacts/owned-display
```

The generated manifest pins the backend and publisher hashes. Gradle packages
both as dormant runtime assets; no installation/launch registration is added.
DEX instruction generation uses D8's supported API 36 instruction floor. Host
admission and the app's min/target SDK remain 37; this is not an Android 16 mode.
Binder C++ helper header provenance and Apache license are in
`third_party/android-binder/`. The app's license screen also includes the static
NDK C++ runtime's Apache/LLVM exception notice.

For a **separate disposable AOSP source build**, the overlay tool checks all
input hashes from `compatibility/android17-owned-display-inputs.json` and writes
new files below ignored `artifacts/`. It reads, never modifies, its input tree:

```sh
python3 tools/prepare_owned_display_vmm.py \
    --checkout /path/to/pinned/external/crosvm \
    --backend artifacts/owned-display/libplb_owned_display_backend.so \
    --output artifacts/owned-display-overlay
```

The overlay adds `libplb_vmm_android17`, a single-use C entry without a raw window
pointer, and replaces the source tree's GPU display link dependency with the
private backend. It leaves the renderer/export/normal GPU flush implementations
unchanged. Exact source-overlay generation was exercised locally. **The complete
Soong build/link graph has not been built or qualified.** Apply this only to a
private build checkout, never the source tree used to build an Android system
image/APEX. Build only the separate library; never install/flash the source
tree's stock `crosvm`, APEX or Android image. Its renderer/runtime dependencies,
qualified launcher, artifact admission and resource/memory protection still
need integration. The Mac does not acquire a full AOSP checkout.

## Debian and Terminal hooks

The Android 17 APK's classes/signatures were rechecked against the saved
Terminal 37. `TerminalContract` now checks field types, void/instance method
signatures, terminal key mapping and inherited view teardown **before** installing
any launch hook. All required deoptimizations are checked; partial installations
are removed on failure. Scope remains only `com.android.virtualization.terminal`.

A concrete lifetime defect was repaired: a Fragment can outlive its WebView.
Previously the controller treated a non-null old view as already bound, leaving
its console attached to a detached view. The inherited AndroidX
`Fragment.onDestroyView()` hook cleans up only confirmed TerminalTabFragments;
`performDestroyView()` is also deoptimized. Rebinding checks the actual new view.
Closing releases pending output waiters, and channel creation is serialized with
shutdown so a late JavaScript callback cannot open a closed tab's channel.
Neither tab cleanup nor an Activity close destroys the persistent Debian disk.

This is a lifecycle repair, **not verified Debian boot on Android 17**. The
existing separate virgl/GUI/network/audio paths and guest data remain intact.
The standard VM start/connection hooks still replace only the exact inspected
Terminal. No framework/SystemUI/other keyboard/application hooks are introduced.

A shell-only device probe is available for the newly authorized USB checks:

```sh
python3 tools/build_terminal_contract_probe.py --sdk /path/to/sdk
```

Its dedicated `app_process` loads the pinned Terminal's classes without class
initialization, checks host/APK identity and reflects the full contract. It does
not instantiate an Activity, install hooks, start a VM, request root or manipulate
the GUI. Transfer/run it only on the explicitly selected Galaxy, then remove only
its temporary test files. A matching reflection result does not prove Vector
interception/deoptimization or a successful console/VM session.

The user authorized tests requiring neither a soft reboot nor GUI operation,
then explicitly chose wireless ADB when Galaxy USB discovery was unavailable.
Only the selected Galaxy was queried; no Pixel command was sent.

Device results:

- SM-S948Q / SDK 37 / S948QOPU1BZI9 / exact inspected kernel release matched.
- Installed stock crosvm and Microdroid guest-kernel SHA256 matched the ROM pins.
- The shell-only Terminal probe returned `TERMINAL_CONTRACT_OK`, inherited view
  teardown `androidx.fragment.app.Fragment`, and four deoptimization callers.
  This is reflection verification, not execution of Vector deoptimization.
- `tools/owned_display_load_probe.c` loaded the owned ARM64 backend and resolved
  all nine display C ABI entries plus real Binder-pool/AHB import entries. It
  only uses `dlopen`/`dlsym`/`dlclose`; it calls none of those display operations.
- Both probes removed their own temporary device files. Boot and core process
  identities stayed unchanged, taint stayed 4608, SELinux stayed Enforcing,
  and the six shmem policies stayed `never`. AVB reported green / BL locked.
- No Activity, hook installation, VM, frame submission, root-policy request,
  host setting change or reboot was performed. Host identity/reflection/linkage
  tests are not Debian/GPU boot, stability or fence validation.

## Subsequent non-GUI CPU VM checks

The no-VM statement above describes the reflection/linkage probes only. The user
then explicitly authorized non-GUI VM checks, and two separate 512 MiB / one-vCPU
protected CPU VMs completed: diskless, then a fixed Terminal root_part attached
read-only. Each guest checked 16 MiB with a nonce-bound completion marker and
exited normally. The second mounted ext4 read-only with `noload` and ran guest
`dpkg --print-architecture`, returning arm64. The full source image hash matched
before/after. No existing writable disk was attached.

The Microdroid guest reported
`6.6.118-android15-8-gde6599b44559-ab14720466`, distinct from host 6.12.69. Logs
include a missing guest watchdog device (-19) and a shutdown closed-channel VCPU
control warning, followed by positive guest and VMM completion. They are not
omitted as if the logs were warning-free. Host boot/core process identities,
trusted VM identity, taint 4608, Enforcing and the six `never` settings remained
unchanged. These tests establish CPU/readonly-guest progress, not the new display
handoff, GPU frames, writable Debian services, full Android guest startup or
long-running stability.

`tools/build_android17_cpu_probe.py` builds fresh private nonce-bound diagnostic
artifacts and never connects to a device. The runner additionally requires the
positive console markers, rather than treating VMM exit 0 as success. Its result
parser has wrong-nonce, wrong-architecture, ordering and failure-case tests.

The user later soft-rebooted the host themselves and requested a pause. No device
connection or further trial has been attempted after that instruction.
