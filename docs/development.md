# Development

## Device invariants

Temporary root and a locked bootloader are the intended environment. No kernel update, replacement, patching or flashing; no partition writes, SELinux changes, global virtualization/network properties, reboot or root-method changes. Only the device's existing `/apex/com.android.virt/etc/fs/microdroid_kernel` is used. The C init and musl agent are guest user-space programs.

## Stock app contract

The tested stock app throws `UnsupportedOperationException` when it requests a non-protected VM. The module gates on SDK 36 / Terminal versionCode 36, resolves the actual fields and methods first, and rolls back partial hooks. Only the main process of `com.android.virtualization.terminal` is scoped.

| Hook | Purpose |
|---|---|
| `MainActivity.startVm()` | Replace the unsupported native VM launch |
| `MainActivity.connectToTerminalService(TerminalTabFragment)` | Bind the fragment to its PTY |
| `TerminalTabFragment.onViewCreated(View, Bundle)` | Also bind restored UI state |
| `TerminalTabFragment.onDestroy()` | Close only that tab's transport |
| `MainActivity.onDestroy()` | Close all activity transports and display |

Verified callers are deoptimized as needed. Native tab controls are retained. Native disk settings are disabled because they do not control this private disk. The display button opens the local noVNC dialog. No system_server/SystemUI/Zygote hooks are installed.

Configuration uses libxposed 102 remote preferences. Framework status means the module is loaded, not that the VM has booted. Unknown configuration shows a setup error rather than entering the known failing native startup; explicit OFF restores stock behavior.

## Root boundary

The app requests `su -M` to start a bounded native manager. `-M` selects the existing global mount namespace for that child: Android app-data isolation otherwise hides the real installed image. It does not remount or change global policy. Primary-user UIDs come from PackageManager.

The abstract Unix socket `net.ryuya_dev.protectedlinux.console` authenticates peers using `SO_PEERCRED`. Only the installed Terminal UID can request a PTY (`R`) or GUI (`V`). Module UID/root may query or stop the manager; Terminal cannot request its global stop. Java and native clients verify the server UID is root. No shell command strings are accepted by the host service. Console bytes go only to the guest.

Private files live under `/data/user/0/net.ryuya_dev.protectedlinux/files/vm`, with root-owned mode 0700 storage. The stock Terminal's `files/linux/root_part` is read only during initial sparse copying. Owned qcow2 data uses that private immutable copy as backing. Cached base identity is validated; mismatches stop instead of rebuilding over user data.

Existing crosvm uses Gunyah `--protected-vm-without-firmware`, 512 MiB, one vCPU, no balloon, private qcow2 and the owned raw initramfs. This omits pvmfw verified boot and attestation. The guest PID1 agent exposes only host-CID vsock PTY/GUI/shutdown operations. `/dev`, `/run` and `/tmp` are guest tmpfs.

The manager caps sessions at 12 and VM life at 30 minutes. Last disconnect requests guest sync/poweroff, then bounded fallback terminates only tracked owned processes. Idle helper expiry is 30 minutes. Child parent-death signals are restored after credential drops. The unprivileged proxy closes inherited manager/client Unix descriptors before entering its network loop. Helper identity includes a native source fingerprint; stale helpers are stopped before new launch. Extraction uses atomic rename to avoid overwriting running executables.

Network and GUI details are in [the architecture record](architecture.md). Neither relies on host TCP listeners or host route/firewall changes. Local private diagnostics contain startup/errors only, not console payloads. Proxy logs exclude destinations and credentials.

## Build

Requirements: JDK 21, Android SDK API 37, NDK `28.2.13676358`, Python 3; Gradle wrapper 9.6.1 / AGP 9.3.2. App target/min API 36. Set `sdk.dir` in untracked `local.properties`.

```sh
python3 -m unittest discover -s tests -v
./gradlew :app:assembleRelease :app:lintRelease
```

`tools/build_native.py` compiles Android manager/proxy, freestanding guest init and a static musl guest agent. First build downloads the pinned musl 1.2.5 source, validates SHA256 and builds only libc/user-space. NDK compiler-rt builtins are linked. No kernel is downloaded or built. Raw cpio is used because this existing guest kernel has no gzip-initramfs support.

Set `PROTECTED_LINUX_SIGNING_PROPERTIES` to a private properties file with `storeFile`, `storePassword`, `keyAlias`, `keyPassword` for a signed release. Never commit the file/key. Without it, the release APK is unsigned. The output is `app/build/outputs/apk/release/app-release.apk`.

Dependency licensing checks the resolved runtime set. Full bundled web/native licenses are included separately. noVNC ES modules are unmodified; custom transport and UI are in `assets/web/display.html`. See `third_party/*provenance.json` for pinned sources. APK/kernel/Linux images, downloaded archives, device traces and signing material are not source assets.

## Testing

`tests/test_stream_relay.py` runs the real relay with socketpairs, binary data larger than buffers, simultaneous backpressure, half-close, final PTY output and idle deadlines. It needs a host C compiler.

`tests/proxy_policy_probe.c` includes the real proxy policy functions. Compile it with NDK clang and run on Android as an unprivileged UID (or build on Linux); it starts neither proxy nor VM. It checks authority parsing and forbidden address families. `tools/vm_console_probe.c` is an explicit device diagnostic; run only as the selected Terminal UID. It refuses root and checks root server credentials. It is not packaged in the APK.

Device tests must specify the selected ADB serial; never operate on another attached device. A root adbd is not an untrusted-peer test: explicitly drop to UID 2000. After tests, remove diagnostic binaries from `/data/local/tmp`.

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
- [Gunyah on Snapdragon guide](https://github.com/polygraphene/gunyah-on-sd-guide) — its kernel/SELinux modification steps are outside this project's constraints.
- [libxposed API](https://jingmatrix.github.io/libxposed-api/io/github/libxposed/api/XposedInterface.html)
- [libxposed service](https://libxposed.github.io/service/)
- [noVNC v1.6.0](https://github.com/novnc/noVNC/tree/v1.6.0)
- [musl releases](https://musl.libc.org/releases/)
