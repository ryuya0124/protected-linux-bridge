# Galaxy validation — 2026-10-18

## Existing device baseline

- SM-S948Q, Android 16, One UI property `80500`
- Build `S948QOPS1AZHL_SJP1AZHL`
- Host kernel `6.12.30`; existing Microdroid guest kernel `6.6.98-android15-8-ga95f39af1cac-ab13905842`
- Gunyah present; protected VM support true; non-protected VM support absent
- Standard Terminal versionName 16 / versionCode 36
- Temporary KernelSU root; `su -M` supported; SELinux Enforcing

## Actually verified

| Check | Result |
|---|---|
| Whole stock Terminal package enabled | Installer opens; activity-only enabling had failed |
| Native launch on this device | Non-protected VM exception reproduced and traced |
| Protected VM with existing guest kernel | Boots Debian 13.3; no kernel replacement |
| Stock Terminal UI + API 102 hook | Console opens, input and `/etc/os-release` work |
| Read-only base image | `touch /plb-test` returns `Read-only file system` |
| Separate guest kernel | `uname -r` differs from host; guest boot ID differs |
| Root helper query while VM runs | Readiness returned; no second VM created |
| Untrusted UID 2000 socket access | Rejected before console access |
| Terminal force-stop | Owned VM stops; idle helper remains |
| Existing host boot ID | Unchanged through validation; no host/soft reboot |
| SELinux | Remains Enforcing |
| Existing kernel vsock + Gunyah | Guest CID 39820 exchanges probe/ACK with host, exits normally |
| Release build and Lint | Build 14 passes; Lint has compatibility/reflection/style warnings, no errors |

A root ADB-forward connection is not an untrusted-UID test on this device: its adbd runs with UID 0. The actual rejection test explicitly dropped the probe process to UID 2000.

ADB's very fast synthetic `input text` burst duplicated text in the xterm/WebView input path. Per-character synthetic input with a short interval worked. Broad IME testing is still outstanding; do not claim all keyboards or rapid automated input are validated.

## Work in progress

The base read-only single-console implementation is verified. Full networking, GUI and multiple tabs are not yet implemented/verified. Existing guest config has `CONFIG_NETDEVICES`, DRM, framebuffer and overlayfs disabled, while virtio-vsock and PTYs are present. Therefore the extended design will use vsock, guest user-space proxy/PTYs/software display, and an owned copy-on-write guest disk. It must not alter the stock base disk or install another kernel.

Opening/closing tabs, missing-launcher recovery, post-update reload, on/off behavior and installer buttons need final validation for the current APK. Home/back may move the Terminal task into the background without destroying it; explicit tab close or `exit` semantics must be tested separately.

Private screenshots, complete traces, VM boot IDs, serials and downloaded OS assets are intentionally excluded from the repository.
