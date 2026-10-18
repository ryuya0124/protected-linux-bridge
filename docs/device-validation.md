# Galaxy validation — 2026-10-18

## Device baseline

- SM-S948Q, Android 16, One UI property `80500`
- Build `S948QOPS1AZHL_SJP1AZHL`
- Host kernel `6.12.30`; preinstalled Microdroid guest kernel `6.6.98-android15-8-ga95f39af1cac-ab13905842`
- Gunyah protected VM support present; non-protected VM support absent
- Standard Terminal versionName 16 / versionCode 36
- Temporary KernelSU root, `su -M`, Vector API 102; SELinux Enforcing

These observations describe this build only. They are not a claim that all Snapdragon devices or later Android versions work.

## Verified functionality

| Check | Observed result |
|---|---|
| Native stock startup | Non-protected VM exception reproduced and traced |
| Protected VM with existing kernel | Debian 13.3 boots; no kernel replacement |
| Stock Terminal with API 102 hooks | Prompt, command input and `/etc/os-release` work |
| Native tab add/switch/close | Two independent PTYs: first stays in `/tmp`, second starts in `/root`; closing second preserves first's working directory |
| Last native tab close | Terminal activity closes and owned VM exits; unrelated existing trusted VM is untouched |
| Writable guest disk | Write/read `/root/plb-verify.txt`; data survives VM and helper restart |
| Original stock image | SHA256 identical before and after writable/apt tests |
| HTTPS through production proxy | `curl -I https://deb.debian.org/debian/README` returns HTTP/2 200 with normal TLS verification |
| GUI installation | `plb-install-gui` exits 0; TigerVNC, xterm, Openbox installed inside the guest |
| GUI display | Openbox/xterm shown in the stock Terminal's display dialog; reopen works; build 27 fresh-VM repetition renders without a desktop tap |
| GUI input | Tap xterm, type `echo GUI_OK` in draft, press Enter; guest prints GUI_OK |
| Confirmed stop button | Stops the owned manager/VM; UI reports stopped; unrelated trusted VM remains |
| Helper binary update | New native fingerprint stops/replaces old helper; persisted Linux disk remains usable |
| Enable/installer buttons | Module enables package/activities and starts InstallerActivity; an already installed image is recognized and the stock app returns to Terminal |
| Untrusted UID 2000 | Console request rejected before access (`VM_STATUS=255`) |
| Proxy credentials | Actual child UID/GID is Terminal UID; supplementary inet only, CapEff=0, NoNewPrivs=1 |
| Proxy policy probe | 27 authority/address rejection checks pass as UID 2000 |
| Host relay regression tests | 3 tests pass: duplex binary/backpressure/half-close, final output flush, deadline |
| Signed release and Lint | Build 27, version 0.1.0-preview.2 (code 3), signature verification succeeds; Lint has reflection/compatibility/style warnings, no errors |
| Host boot and SELinux | Boot ID unchanged throughout; SELinux remains Enforcing |

The original image SHA256 was `975d952a1027537490ae8466b2a957b3a99401e9bfb40e1d70ee68090d12fd2d` both before and after the guest writes/package installations. Device serials, full traces, boot IDs, APKs and downloaded OS images are private and excluded from this repository.

## Findings and limits

- The existing guest kernel has NETDEVICES, DRM, FB and OVERLAY_FS disabled; virtio-vsock, PTYs and loopback work. The implementation uses guest user-space and an owned qcow2 disk instead of a kernel change.
- Networking initially failed when the module UID was background-restricted. Android reported DOZE and APP_BACKGROUND blocks. The proxy now runs as Terminal UID; foreground Terminal networking works. No firewall, VPN, Doze whitelist or DNS policy was changed. Background persistence/communication is not guaranteed.
- GUI initially appeared black until pointer activity. The viewer now initializes the guest pointer without clicking and requests one full initial frame; the desktop initializes its root background, and the guest screen saver is disabled. Fresh-VM repetition with no desktop touch rendered within the seven-second observation window. First load may take several seconds. This is not GPU passthrough.
- Rapid synthetic ADB input can duplicate/corrupt xterm IME input. Per-character input with short pauses works. The GUI uses an explicit draft/send field to preserve composition and spaces. Japanese text delivery was seen, but not all IMEs, Unicode fonts or physical keyboards were tested.
- The private guest filesystem is about 2.8 GiB. After GUI/debug package installation it had about 1.1 GiB free. The shipped APK does not include those Debian packages.
- Main app text follows Japanese/English OS language. Some injected transport errors are currently Japanese.
- This is `--protected-vm-without-firmware`, with no pvmfw verified boot or attestation. Passing functional tests is not proof of isolation against a malicious hypervisor or kernel.
- Full UDP/ICMP networking, audio, GPU acceleration, arbitrary desktop environments, disk resizing, multiuser support and OS-update compatibility have not been established.

## Reproduce without modifying the host OS

1. Confirm the exact supported OS/Terminal version, root and Vector scope.
2. Prepare the helper, launch Terminal, run `cat /etc/os-release`, `uname -r`, `pwd`.
3. Add another native tab; change the first tab's working directory; switch/close the second and confirm the first persists.
4. Write a file in `/root`, close all sessions, open again and verify it.
5. Keep Terminal foreground and run `curl -I https://deb.debian.org/debian/README`.
6. Run `plb-install-gui`; open the display, tap xterm, send a command using the input field.
7. Stop via the module's confirmed stop button. Check only the owned VM/helper stop; reopen via Prepare.
8. Explicitly drop a diagnostic client to UID 2000 and confirm rejection. A root adbd is not an untrusted-user test.
9. Compare the stock image hash and host boot ID, verify Enforcing, remove diagnostic binaries.
