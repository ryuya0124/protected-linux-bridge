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
| Proxy policy probe | 43 authority/address, UDP hostname and ICMP validation checks pass as UID 2000 |
| Host relay regression tests | 10 tests pass: relay integrity/backpressure/deadlines, datagram framing/limits, VTest negotiation/rejection |
| Signed release and Lint | Version 0.2.0-preview.1 (code 4), signed release build and Lint pass; warnings remain, no errors |
| Real IPv4 ICMP | Guest `ping -c 2 1.1.1.1`: 2/2 replies, 0% loss; Terminal-UID SOCK_DGRAM ping socket, no raw capability |
| UDP DNS | Guest forwarder to 1.1.1.1:53 returns two example.com A answers; query transaction ID matched |
| UDP boundaries and concurrency | Four simultaneous local clients, 20 exact echo round trips at sizes 0/1/16/1200/8000; no global host network changes |
| GPU EGL | Production bridge reports virgl (Adreno (TM) 840), OpenGL 2.1 and GLES 3.2 |
| Actual GPU readback | Guest EGL glClear/glReadPixels returns [64,128,191,255], exit 0; not only a driver-name check |
| X11 / Xwayland GPU | glxinfo on both Xvnc :1 and Weston's Xwayland :0 reports Accelerated: yes and virgl Adreno 840; glxgears draws |
| GPU desktop | Headless probe and shipped X11-backed Weston 14.0.2 log GL renderer Adreno 840; standard Terminal display shows Weston/Wayland Terminal |
| Viewer | DISPLAY_COMPOSITOR hardware=true and DISPLAY_TRANSPORT binary; desktop renders through bounded ArrayBuffer delivery |
| GPU worker credentials | Watcher, renderer parent and renderer child all UID/GID 9999, no supplementary groups, CapEff=0, NoNewPrivs=1 |
| GPU mode save/recovery | Mode survives VM stop/restart; OFF gives llvmpipe again. Explicit stop leaves no owned GPU server descendants |
| Vulkan negative check | Venus-only ICD with VN_DEBUG=vtest reports protocol version 0 too old; vulkaninfo exits 1. GPU Vulkan is not claimed |
| Host boot and SELinux | Boot ID unchanged throughout; SELinux remains Enforcing |

The original image SHA256 was `975d952a1027537490ae8466b2a957b3a99401e9bfb40e1d70ee68090d12fd2d` both before and after the guest writes/package installations. Device serials, full traces, boot IDs, stock/vendor APKs and downloaded OS images are private and excluded from this repository.

## Findings and limits

- The existing guest kernel has NETDEVICES, DRM, FB and OVERLAY_FS disabled; virtio-vsock, PTYs and loopback work. The implementation uses guest user-space and an owned qcow2 disk instead of a kernel change.
- Networking initially failed when the module UID was background-restricted. Android reported DOZE and APP_BACKGROUND blocks. The proxy now runs as Terminal UID; foreground Terminal networking works. No firewall, VPN, Doze whitelist or DNS policy was changed. Background persistence/communication is not guaranteed.
- GUI initially appeared black until pointer activity. The viewer now initializes the guest pointer without clicking and requests one full initial frame; the desktop initializes its root background, and the guest screen saver is disabled. Fresh-VM repetition with no desktop touch rendered within the seven-second observation window. First load may take several seconds. This is not GPU passthrough.
- Rapid synthetic ADB input can duplicate/corrupt xterm IME input. Per-character input with short pauses works. The GUI uses an explicit draft/send field to preserve composition and spaces. Japanese text delivery was seen, but not all IMEs, Unicode fonts or physical keyboards were tested.
- The private guest filesystem is about 2.8 GiB. After GUI/debug package installation it had about 1.1 GiB free. The shipped APK does not include those Debian packages.
- Main app text follows Japanese/English OS language. Some injected transport errors are currently Japanese.
- This is `--protected-vm-without-firmware`, with no pvmfw verified boot or attestation. Passing functional tests is not proof of isolation against a malicious hypervisor or kernel.
- Networking is explicit UDP forwarding / IPv4 echo, not a full IP network. IPv6 ping, multicast/broadcast and incoming Internet connections are not provided. 65507-byte datagram framing passed socketpair regression tests; real external echo was tested through 8000 bytes, not 65507.
- OpenGL and Weston composition use the host GPU; VNC capture, encoding, memory copies and application-specific CPU 2D drawing remain. No zero-copy transport, GPU Vulkan, audio, arbitrary desktop environments, disk resizing or multiuser support is established. There is no general performance/FPS guarantee.
- This update's automated GUI text-input attempt through ADB/Simeji produced altered draft text. The existing draft/send mechanism remains; the new Weston/Simeji input combination needs a manual IME check. Do not count this failed injection as a successful new GUI text-input test.

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


## 0.2 preview GPU/network reproduction

- Keep the selected Terminal foreground. Use `ping -c 2 1.1.1.1`. Run `plb-udp-forward 5353 1.1.1.1 53` in one tab; send a DNS query to guest 127.0.0.1:5353 from another.
- Install GUI with `plb-install-gui`, then GPU packages with `plb-install-gpu`; stop and recreate only the VM. No Android restart.
- `plb-gpu-info` and `glxinfo -B` identify the actual renderer. Run `tests/gpu_guest_probe.py` inside the guest to check the actual framebuffer result. Existing DRM-less guest kernel remains unchanged.
- Open the display button and inspect private `/tmp/plb-weston.log` for GL renderer. `DISPLAY=:0 glxinfo -B` checks Xwayland. `DISPLAY=:1` checks the outer Xvnc display. Module logcat distinguishes the hardware-composition flag and binary transport.
- `plb-gpu-mode off` + VM recreation restores CPU rendering; `plb-desktop-mode classic` keeps Openbox while optionally leaving individual OpenGL GPU programs available.
- Stop the VM and check only the tracked owned GPU descendants disappear. Do not terminate unrelated trusted VMs. The renderer runtime directory contains only public code, libraries and an empty socket directory; diagnostic traces stay in private module data.
