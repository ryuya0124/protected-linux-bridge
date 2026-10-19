# Development

## Device invariants

Temporary root and a locked bootloader are the intended environment. No host kernel/module/boot or partition changes, SELinux changes, global virtualization/network properties, reboot or root-method changes. Guest-only kernels are permitted under the updated policy; stock `/apex/com.android.virt/etc/fs/microdroid_kernel` remains the default and is never overwritten. See `guest-kernel-plan.md`. The C init and musl agent are guest user-space programs.

## Stock app contract

The tested stock app throws `UnsupportedOperationException` when it requests a non-protected VM. The module gates on SDK 36 / Terminal versionCode 36, resolves the actual fields and methods first, and rolls back partial hooks. Only the main process of `com.android.virtualization.terminal` is scoped.

| Hook | Purpose |
|---|---|
| `MainActivity.startVm()` | Replace the unsupported native VM launch |
| `MainActivity.connectToTerminalService(TerminalTabFragment)` | Bind the fragment to its PTY |
| `TerminalTabFragment.onViewCreated(View, Bundle)` | Also bind restored UI state |
| `TerminalTabFragment.onDestroy()` | Close only that tab's transport |
| `MainActivity.onDestroy()` | Close all activity transports and display |

Verified callers are deoptimized as needed. Native tab controls are retained. Native disk settings are disabled because they do not control this private disk. The display button opens the GLES or compatibility noVNC viewer. A separate unexported DesktopActivity provides fullscreen viewing without closing Terminal PTYs. No system_server/SystemUI/Zygote hooks are installed.

Configuration uses libxposed 102 remote preferences. Framework status means the module is loaded, not that the VM has booted. Unknown configuration shows a setup error rather than entering the known failing native startup; explicit OFF restores stock behavior.

## Root boundary

The app requests `su -M` to start a bounded native manager. `-M` selects the existing global mount namespace for that child: Android app-data isolation otherwise hides the real installed image. It does not remount or change global policy. Primary-user UIDs come from PackageManager.

The abstract Unix socket `net.ryuya_dev.protectedlinux.console` authenticates peers using `SO_PEERCRED`. Only the installed Terminal UID can request a PTY (`R`). Terminal and module UIDs can request a GUI (`V`); the module needs no additional Xposed scope. Module UID/root may query or stop the manager; Terminal cannot request its global stop. Java and native clients verify the server UID is root. No shell command strings are accepted by the host service. Console bytes go only to the guest.

Private files live under `/data/user/0/net.ryuya_dev.protectedlinux/files/vm`, with root-owned mode 0700 storage. The stock Terminal's `files/linux/root_part` is read only during initial sparse copying. Owned qcow2 data uses that private immutable copy as backing. Cached base identity is validated; mismatches stop instead of rebuilding over user data.

Existing crosvm uses Gunyah `--protected-vm-without-firmware`, configurable memory/CPU (defaults512 MiB/one vCPU), no balloon, private qcow2 and the owned raw initramfs. This omits pvmfw verified boot and attestation. The guest agent (PID1 or a systemd service) exposes only host-CID vsock PTY/GUI/shutdown operations. `/dev`, `/run` and `/tmp` are guest tmpfs.

The manager caps sessions at 12 and VM life at 30 minutes. Last disconnect requests guest sync/poweroff, then bounded fallback terminates only tracked owned processes. Idle helper expiry is 30 minutes. Child parent-death signals are restored after credential drops. The unprivileged proxy closes inherited manager/client Unix descriptors before entering its network loop. Helper identity includes a native/build-script fingerprint; stale helpers are stopped before new launch. Extraction uses atomic rename to avoid overwriting running executables.

Network and GUI details are in [the architecture record](architecture.md). Neither relies on host TCP listeners or host route/firewall changes. Local private diagnostics contain startup/errors only, not console payloads. Proxy logs exclude destinations and credentials.

## Build

Requirements: JDK 21, Android SDK API 37, NDK `28.2.13676358`, Python 3; Gradle wrapper 9.6.1 / AGP 9.3.2. App target/min API 36. Set `sdk.dir` in untracked `local.properties`.

```sh
python3 -m unittest discover -s tests -v
./gradlew :app:assembleRelease :app:testDebugUnitTest :app:lintRelease
```

`tools/build_native.py` compiles Android manager/proxy, freestanding guest init and a static musl guest agent. First build downloads the pinned musl 1.2.5 source and the pinned Termux Android GPU user-space package, verifies their SHA256 values and builds only libc/user-space. The GPU package is extracted selectively; it never installs or updates Termux or a kernel. NDK compiler-rt builtins are linked. The guarded Android 1.5 GiB manager/backing variant is bundled separately; see [its implementation and validation](android-large-profile.md). The ordinary APK build does not download/build a kernel; the separate `kernels/` recipe does. Raw cpio is used because this existing guest kernel has no gzip-initramfs support.

Set `PROTECTED_LINUX_SIGNING_PROPERTIES` to a private properties file with `storeFile`, `storePassword`, `keyAlias`, `keyPassword` for a signed release. Never commit the file/key. Without it, the release APK is unsigned. The output is `app/build/outputs/apk/release/app-release.apk`.

Dependency licensing checks the resolved runtime set. Full bundled web/native licenses are included separately. noVNC ES modules are unmodified; custom transport and UI are in `assets/web/display.html`. See `third_party/*provenance.json` for pinned sources. APK/kernel/Linux images, downloaded archives, device traces and signing material are not source assets.

## Testing

`tests/test_stream_relay.py` runs the real relay with socketpairs, binary data larger than buffers, simultaneous backpressure, half-close, final PTY output and idle deadlines. It needs a host C compiler.

`tests/proxy_policy_probe.c` includes the real proxy policy functions. Compile it with NDK clang and run on Android as an unprivileged UID (or build on Linux); it starts neither proxy nor VM. It checks authority parsing and forbidden address families. `tools/vm_console_probe.c` is an explicit device diagnostic; run only as the selected Terminal UID. It refuses root and checks root server credentials. It is not packaged in the APK.

Device tests must specify the selected ADB serial; never operate on another attached device. A root adbd is not an untrusted-peer test: explicitly drop to UID 2000. After tests, remove diagnostic binaries from `/data/local/tmp`.

`tests/test_vm_protocols.py` also compiles the production datagram/VTest helpers and checks zero-length/max-size records, fragmentation, truncation, buffer limits, v0 negotiation and unknown/malformed protocol rejection. `tests/gpu_guest_probe.py` runs inside the guest through `plb-gpu-run`: it clears an actual EGL framebuffer and validates pixel readback.

The Android policy probe now includes UDP destination syntax and ICMP echo validation (43 checks). Production UDP was also exercised with four independent clients and 0/1/16/1200/8000-byte echoes, plus a real DNS query. 65507-byte framing is a host socketpair test, not a claim that all external paths support that datagram size.

## Diagnostics and recovery

```sh
adb -s DEVICE logcat -s ProtectedLinux
adb -s DEVICE shell su -M -c 'cat /data/user/0/net.ryuya_dev.protectedlinux/files/vm/crosvm-bridge.log'
adb -s DEVICE shell su -M -c 'cat /data/user/0/net.ryuya_dev.protectedlinux/files/vm/network-proxy.log'
```

`HOOK_READY`, `CHANNEL_CONNECTED R/V` and `CHANNEL_FAILED` distinguish hook load from guest connections. Network failures may come from Android app/VPN/Doze policy; never disable firewall/SELinux/power policy to conceal them. Logs are private and should be reviewed/redacted before sharing.

For emergency stop use the app button, or the exact owned helper:

```sh
adb -s DEVICE shell su -M -c '/data/user/0/net.ryuya_dev.protectedlinux/files/runtime/debian-bridge --stop'
```

Disable in Vector and close Terminal to remove hooks. A missing root environment needs restoration through the user's existing method, not automatic exploitation. Do not remove a private base while a dependent qcow2 disk exists. Stop the VM before any offline disk backup.

## Primary references

- [AOSP VirtualMachineConfig](https://android.googlesource.com/platform/packages/modules/Virtualization/+/main/libs/framework-virtualization/src/android/system/virtualmachine/VirtualMachineConfig.java)
- [Gunyah on Snapdragon guide](https://github.com/polygraphene/gunyah-on-sd-guide) — host kernel/SELinux modifications are outside this project's constraints; guest-only kernel builds are now allowed.
- [libxposed API](https://jingmatrix.github.io/libxposed-api/io/github/libxposed/api/XposedInterface.html)
- [libxposed service](https://libxposed.github.io/service/)
- [noVNC v1.6.0](https://github.com/novnc/noVNC/tree/v1.6.0)
- [musl releases](https://musl.libc.org/releases/)


GPU reference sources: [Mesa VirGL](https://docs.mesa3d.org/drivers/virgl.html), [Termux build and patches](https://github.com/termux/termux-packages/tree/5952664fae176ea6929f354bf1b1af70c4a1cffb/packages/virglrenderer-android), [AOSP Mesa vtest winsys](https://android.googlesource.com/platform/external/mesa3d/+/refs/heads/android16-release/src/gallium/winsys/virgl/vtest/virgl_vtest_socket.c), [upstream VTest protocol](https://gitlab.freedesktop.org/virgl/virglrenderer/-/blob/1.3.0/vtest/vtest_protocol.h). Exact runtime provenance and full licenses are in `third_party/gpu-runtime-provenance.json` and `third_party/gpu/` and are included in the app license screen. GPU runtime retrieval tries the official package first and a release-asset mirror of the same archive second; both must match the pinned SHA256. Preserve the checksum-verified cache for offline rebuilds.


The display transport uses AndroidX WebKit 1.17.1 ArrayBuffer messages only when both WEB_MESSAGE_LISTENER and WEB_MESSAGE_ARRAY_BUFFER are supported. A fixed HTTPS origin and main-frame check guard the ready handshake; legacy string delivery remains the fallback. JavaScript consumption acknowledgements bound the native queue. GPU viewer composition is requested through the Dialog window flag and its actual isHardwareAccelerated state is logged. [AndroidX binary reply API](https://developer.android.com/reference/androidx/webkit/JavaScriptReplyProxy#postMessage(byte%5B%5D)).

[Venus VTest source](https://android.googlesource.com/platform/external/mesa3d/+/refs/heads/android16-release/src/virtio/vulkan/vn_renderer_vtest.c) shows the minimum v3 protocol, resource FD receipt with SCM_RIGHTS and memory mapping. This bridge's fd-free OpenGL v0 transport cannot substitute for that shared-memory mechanism.

## 0.3 display and configuration

`RawRfb` is an Android-free RFB 3.8 parser with raw RGBX/desktop-resize support only. Handshake authentication is None inside the existing authenticated VM channel; this is not a network listener. The socket peer must be UID 0, and the manager authenticates the caller UID. A framebuffer is bounded to 4,194,304 pixels, maximum width4096/height4096 (16 MiB). Rectangles, clipboard/name sizes and update payloads have independent bounds. Unknown encodings/protocols fail closed; compatibility mode is explicitly selectable.

`GpuDisplayController` reads native socket bytes into direct ByteBuffers through `Os.read`, waits for the GL thread to upload the damage, then requests another incremental frame. One mutable framebuffer and one pending upload limit memory/latency. GLES3 handles RGBX alpha, damaged subrectangles, scaling and letterboxing. Context recreation requests a full refresh; hidden surfaces pause the upload deadline. Physical input is translated into RFB keys/pointers, with focus-loss release and bounded output queue. Android system keys remain local. This is a CPU-copy transport with GPU viewer composition, not DMA-BUF/zero-copy or hardware encoding.

Root-owned `graphics-mode` is fixed-schema `MODE RESOLUTION_ID FPS\n`, opened O_NOFOLLOW and validated for type/owner/mode. A former single-mode file migrates through default resolution/fps. `--settings MODE ID FPS` accepts whitelisted integers; arbitrary strings/paths/commands are not accepted. The shared C table in `native/display_modes.h` assigns stable IDs matching the Android menu. Settings reach the guest as command-line parameters of the existing kernel, not by mounting guest filesystems. Linux flags and Xvnc geometry/FrameRate take effect next VM; remote preferences carry viewer choice/fps. Explicit VM recreation has a data-loss confirmation, stops only this VM, then reopens Terminal.

Android JVM tests cover fragmented raw frames, invalid/auth/oversize/truncated messages, every offered resolution, RFB key/pointer serialization and physical-input mappings. Run the full build command above. Logs `RAW_GPU_READY renderer=...`, `DISPLAY_TRANSPORT raw-gles`, `RAW_GPU_FRAME WxH`, or `RAW_GPU_FAILED` distinguish renderer/connect/frame/error state. The native authorization boundary is unchanged for PTYs.

References: [RFB protocol](https://www.rfc-editor.org/rfc/rfc6143), [Os.read(ByteBuffer)](https://developer.android.com/reference/android/system/Os#read(java.io.FileDescriptor,%20java.nio.ByteBuffer)), [GLES30](https://developer.android.com/reference/android/opengl/GLES30), [Surface frame-rate hints](https://developer.android.com/media/optimize/performance/frame-rate). See [security limits](security.md) before changing VMM privileges.

Viewer preferences are isolated in `RuntimePreferences` (Android SharedPreferences only); an ordinary Bridge activity must never resolve compile-only `HookEntry`/XposedModule. Terminal's UI uses an independent ResourcesLoader for the module APK because package visibility may reject createPackageContext; its theme does not inherit colliding host resource IDs. The compatibility page disables its unused WebCodecs VideoDecoder before noVNC import to avoid an optional top-level feature probe blocking initialization; upstream library files remain unmodified.

## 0.4 profiles and integrations

`debian-bridge` retains `files/vm`; `debian-mainline-bridge` is the same manager built with a fixed experimental profile, `files/vm-mainline` and a pinned private guest-image path. One authenticated abstract socket and one root-private ownership lock ensure only one profile/clone owns writable disks at a time. READY includes both build and profile identifiers. Stop/query remain usable even when a guest image fails validation. No caller-supplied executable, image path or shell expression is accepted by the native profile API.

Kernel import uses Android's document picker and a bounded stream into private staging. Java checks the known image size/SHA256; the root installer repeats SHA256 via the installed fixed sha256sum utility, checks type/owner, copies/readback-verifies into a no-follow private directory, fsyncs and renames a readonly image. Unknown builds are rejected. This is a fixed experimental profile, not a generic unverified-kernel importer.

Offline profile initialization refuses a running manager or ambiguous existing destination. It copies/readback-verifies the original qcow2 through readonly input, retains only a hardlink to the root-owned readonly immutable base, and records completion after promotion/fsync. New profile resources default to512 MiB/1CPU/30min/keepalive/systemd, CPU graphics1280x720/30fps. Its writable disk is independent. A failed switch leaves the original available; Java attempts to return to its previous profile when preparation fails. Closing/reopening Terminal may be needed after changing profiles.

`resource_settings.h`, `disk_migration*.h`, `guest_migration.h` implement whitelisted settings and larger-disk migration. Migration boots an isolated offline VM, source block readonly/new block writable, copying then verifying bytes before promotion. Confirmation requires the guest ext4 superblock size to match the requested target. Guest stop acknowledges sync before the host's bounded shutdown. Never reuse the old short forced-stop grace period; it previously lost dirty guest writes in testing.

PulseAudio pipes provide48kHz PCM. AudioTrack/AudioRecord belong to the ordinary module UID, not root/Terminal. A/M IPC is module-only. Background microphone uses an explicitly opted-in microphone foreground service, permission checks, OS indicator and Stop action. AudioDevices uses per-stream preferred routes; the actual route/removal is monitored and mismatches stop the bridge. It does not force global SCO/audio mode. Only connected routes are shown; missing or ambiguous selection fails rather than falling back.

Clipboard C/W requests are bounded64KiB. The optional root-launched helper immediately drops to shell UID2000, uses its existing background permission, fixes only its own Context attribution and fails if that exact context cannot be validated. Only the tracked helper PID may use C/W as shell; arbitrary shell processes are rejected. The public helper directory contains readonly code only. XFixes/inotify drives Xvnc/Xwayland changes, a bounded queue/digests suppress echo; sensitive Android clips are skipped and no content/history is logged.

The UI uses real Material3 BottomNavigationView. Rotation uses negotiated RFB ExtendedDesktopSize with bounded dimensions/screen IDs; no global Android display settings. Both raw GLES and compatibility viewers retain CPU-copy boundaries. A display failure stops I/O rather than repeatedly logging input errors.

Automated checks currently include24 JVM tests and37 native tests, plus release Lint. Physical USB/Bluetooth hardware, Android guests and hardware video are unverified/unimplemented. See the device record for actual sample limits, including Gunyah PAGE_FAULT under heavy experimental-kernel workloads.

## Experimental I/O (0.4.0-preview.2)

The guest tunnel is compiled by `tools/build_tun.py` against the pinned guest musl with NDK28.2 Clang. Archives and every submodule revision/checksum are in `third_party/hev/sources.json`; no moving branch is fetched by the build. The ARM64 static binary is placed in the initramfs, copied into guest `/run` and used only when the full-kernel profile and explicit network switch are on. Upstream source is unmodified; compile-only UAPI libc shims are generated in the ignored cache. Root licences and source notices are included in the app. The total additional cache is about15 MiB, not a new kernel checkout.

The SOCKS5 extension uses command5 UDP-in-TCP, validated header lengths and bounded per-destination connected sockets. TCP uses the existing destination checker/relay; UDP uses the same address policy before connect. Root-private network settings accept only0/1. IPC N is module/root-only. Network UID selection is explicit; do not automatically switch identities to evade VPN or background policy. The chosen module identity requires the ordinary INTERNET permission now declared in the manifest.

The lightweight GPU button saves512MiB/1CPU/960x540/30fps/Openbox+VirGL app rendering/raw GLES viewer, retaining disk size, lifetime, keepalive and systemd. It stops/prepares only this VM and asks the user to reopen GUI. It reduces tested load; it does not fix Gunyah's heavy-workload page-provisioning fault.

Read-only GPU diagnostics: `python3 tools/build_gpu_probe.py --sdk ANDROID_SDK_PATH` creates only host-side artifacts. `guest_gpu_probe.c` refuses to run unless it is PID1. In the isolated guest it mounts the selected disk read-only with `noload`, creates only volatile guest mounts, runs bounded EGL/Vulkan diagnostics and powers off. It must be used only after stopping the owned manager, with the experimental disk explicitly `ro=true`; never pass the original writable disk, reuse a CID/VM socket or run simultaneous writers. It is not installed into production launch flags. Consult [experimental-io.md](experimental-io.md) before interpreting results.

## Preview.3 storage and readability

[Disk management](storage.md) documents the separate offline resize/compaction path, retained previous disk, cancellation socket, usage protocol and crash recovery. Shrink is now explicitly requested; the previous expansion-only preference is superseded. No host mutation exceptions were added. [Host boundary](host-boundary.md) is the exhaustive prohibition list.

First-party source is expanded consistently across Java, C, Python, Gradle, XML, shell and HTML. Test C is in `tests/fixtures/` instead of compressed embedded literals. See [code style and reproducible formatting](code-style.md). Upstream dependency code and wrappers retain their original form.


## Memory diagnostics, 2026-10-19

The [working-set probes](memory-working-set-probes.md) separate configured RAM,
resident page coverage, repeated reuse and VMM accounting. The initial diskless
protected 4 GiB configurations booted with small workloads but failed larger
newly mapped workloads. The later [explicit FD-adoption VMM
and six-size THP experiment](preallocated-vmm-validation.md) completed a 4 GiB
all-byte workload twice. These are developer helpers, not a change to APK
memory admission or released stability claims. Sanitized results are checked in; raw logs stay in
ignored artifacts. The host boundary remains in force; the later narrow small-shmem
trial approval and guarded app integration are documented separately in
[the Android 1.5 GiB record](android-large-profile.md).

### Retired mapped-shmem refault experiment

The larger-memory follow-up found host `Bad page cache` warnings. The mapped
hole-punch helper now returns ENOTSUP without mutating its input, and the
supervisor/runner reject its retired stages. The production 1.5 GiB library is
prefault-only and does not include that helper. BAD_PAGE/OOPS taint or unreadable
health state refuses new diagnostic load and the guarded large app profile;
never clear the host taint or bypass this guard to continue. See
[memory-increase-2026-10-19.md](memory-increase-2026-10-19.md).


### Preview.7 RAM identities and 1536 MiB GLES research

`AndroidGuestProfile` maps the selector to 1536/1792/512 MiB without an unknown
value silently falling back. `tools/android_large_config.h` provides separate
native directories, marker strings, library names and mapping sizes. The build
creates both SHA-pinned manager/library pairs. [Current app evidence](android-large-profile.md).

`tools/build_android_gpu_probe.py --memory 1536 --mode gles` builds the isolated,
prefault-only graphics/memory comparison. The 512 MiB GLES/Vulkan diagnostics
remain separate, and the combined 1536 MiB Vulkan variant is deliberately rejected.
[GPU investigation, safety gates and limitations](android-gpu1536-research.md).

The optional developer-only `:guest-gpu-probe` module builds a fixed signed GLES
proof APK; it refuses the host identity before constructing EGL. Build/test with
`:guest-gpu-probe:assembleRelease :guest-gpu-probe:testDebugUnitTest
:guest-gpu-probe:lintRelease` using the normal private signing environment. Pass
its release APK with `--guest-render-apk` only to the 1536 MiB GLES diagnostic.
The offline `tools/verify_guest_gpu_frame.py` rejects software/stale/invalid
evidence. This test was not reached in the single approved affected-boot trial:
main RAM prefault completed, but only 7.17% of its middle had THP flags and the
unchanged admission gate exited 89. The exceptional one-trial marker is consumed;
the released app remains guarded preview.7. No GPU boot success is claimed.

`tools/audit_gunyah_logs.py` reads a single bounded saved log entirely offline.
It separates raw hypervisor mapping/donation/reclaim values from Linux errno and
RM RPC errors, deduplicates timestamped kernel records within the supplied
single-boot log, and emits no raw addresses or paths. It cannot identify the exact
EL2 allocator or infer a guest pass from VMM exit zero. The
[source and chronology review](gunyah-root-cause-review.md) records the evidence
and the earlier overlooked host page-cache bursts.

`tools/inspect_crosvm_minidebug.py` verifies the pinned saved VMM and extracts its
existing MiniDebugInfo entirely offline. It writes address-bearing ELF/symbol/
assembly output only into a fresh directory under ignored artifacts, without
executing or patching the VMM. [Actual memory-ingress findings](preallocated-ram-handoff.md)
distinguish the installed CMA file-backed route from an original-style shared
memfd adoption design. The later [separate VMM validation](preallocated-vmm-validation.md)
implements that path and verifies a diskless protected 4 GiB all-byte workload
twice. Android and GPU operation are not established by those tests.
