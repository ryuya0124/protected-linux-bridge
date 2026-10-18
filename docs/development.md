# Development

## Non-negotiable device constraints

This project must work with temporary root. **No kernel updates/replacements, flashing, partition writes, reboot, root-method changes, global virtualization-property changes, or SELinux modifications.** The only guest kernel path is the device's existing `/apex/com.android.virt/etc/fs/microdroid_kernel`. The freestanding `native/guest_init.c` is guest user-space init, not a kernel.

## Architecture

The stock app attempts `VirtualMachineConfig.Builder.setProtectedVm(false)` and throws `UnsupportedOperationException` on the tested device. Its installer also needs the whole application enabled; enabling MainActivity alone is insufficient.

The module verifies API 36 and Terminal versionCode 36 and resolves the actual UI contract before installing hooks. Partial installations are rolled back. Scope is the Terminal main process only. It intercepts:

- `MainActivity.startVm()` — prevent the unsupported native VM launch.
- `MainActivity.connectToTerminalService(TerminalTabFragment)` — attach the local console.
- `TerminalTabFragment.onViewCreated(View, Bundle)` — also handle restored UI state.
- `MainActivity.onDestroy()` — close transport and stop the VM.

Callers are deoptimized where required. Unknown versions are not guessed. There are no system_server, SystemUI, Zygote, or game hooks.

The module UI publishes configuration through libxposed remote preferences. A direct module ContentProvider could not be resolved from the tested Terminal process; framework preferences avoid depending on package visibility. Missing configuration shows a setup error rather than entering the known crashing native launch path. Explicit OFF restores original behavior.

The app requests `su -M` for its own bounded helper. Without `-M`, inherited Android app-data isolation hid the real stock image and created private paths in a process-specific mount namespace. `-M` selects the existing global mount namespace for that process; it does not remount filesystems or change policy. No fallback guesses another root mechanism.

An abstract Unix socket accepts the installed Terminal UID only for VM startup. Module UID and root may query readiness. Both Java and native clients verify root peer credentials; the helper verifies `SO_PEERCRED`. Other app and shell UIDs are rejected before VM creation. There is no TCP listener, host-shell command protocol, or console recording. Terminal input goes only to the guest UART.

The helper launches existing crosvm with Gunyah's `--protected-vm-without-firmware`, existing Microdroid kernel, an owned raw cpio initramfs, and a read-only block descriptor for the stock app's `root_part`. Guest init mounts Debian read-only and runs bash inside the VM. Writable `/tmp`, `/run`, and `/dev` are guest tmpfs. No guest network device is supplied.

VM startup is bounded, transport has backpressure/timeouts, output buffering is bounded, and EOF or parent death terminates the owned crosvm child. The helper supports sequential sessions, rejects concurrent VM starts, and expires after 30 minutes idle. VM sessions have a 30-minute maximum. Console text is never interpolated into host commands or executable JavaScript; byte chunks are base64 decoded into xterm buffers.

The WebView loads bundled xterm only, blocks networking/navigation/content/file access, and exposes only console ready/write operations. Its JavaScript bridge does not execute host commands. Standard GUI/add-tab/disk settings are disabled in this preview.

## Build

- JDK 21, Android SDK compile API 37 (target/min API 36)
- Android NDK `28.2.13676358`, Python 3
- Gradle wrapper 9.6.1 / AGP 9.3.2

Create `local.properties` with `sdk.dir`. Then:

```sh
./gradlew :app:assembleRelease :app:lintRelease
```

For a signed release, provide `PROTECTED_LINUX_SIGNING_PROPERTIES` pointing to a private properties file with `storeFile`, `storePassword`, `keyAlias`, `keyPassword`. Never commit it. Without this file the release is unsigned. Do not distribute a debug APK.

`tools/build_native.py` builds the small native helper and guest user-space init; it never downloads or builds a kernel. The generated initramfs is raw cpio because the tested existing guest kernel does not support gzip initramfs. Native sources compile with warnings treated as errors. Dependency-license generation checks the resolved runtime component set. xterm's pinned provenance is in `third_party/xterm-provenance.json`.

## Diagnostics

```sh
adb -s DEVICE logcat -s ProtectedLinux
adb -s DEVICE shell su -c 'cat /data/user/0/com.android.virtualization.terminal/files/protected-linux/crosvm-bridge.log'
```

Use an explicit selected device, never whichever ADB device happens to be first. The private crosvm log contains launcher diagnostics, not guest console input/output. Do not publish full-device logcat, serials, authentication keys, or downloaded system assets.

## References

- [AOSP VirtualMachineConfig](https://android.googlesource.com/platform/packages/modules/Virtualization/+/main/libs/framework-virtualization/src/android/system/virtualmachine/VirtualMachineConfig.java)
- [Gunyah on Snapdragon guide](https://github.com/polygraphene/gunyah-on-sd-guide) — do not apply its kernel or SELinux modification instructions.
- [API 102 remote preferences](https://jingmatrix.github.io/libxposed-api/io/github/libxposed/api/XposedInterface.html)
- [libxposed service reference](https://libxposed.github.io/service/)
