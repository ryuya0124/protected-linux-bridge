# Experimental guest I/O — 2026-10-18

Scope: Galaxy SM-S948Q, Android16 / One UI80500, installed crosvm0.1.0 and Linux6.18.57-plb1. Results are build-specific. All experiments kept the host boot ID unchanged and SELinux Enforcing; no host kernel/module, partition, routing, memory sysctl or protection changes were used.

## Available in preview.2

| Path | Result | Limit |
|---|---|---|
| VTest OpenGL / Adreno840 | EGL ES3.2, GLX `Accelerated: yes`, actual `glxgears` in512MiB/1CPU guest | Tested low-load configuration; heavy workloads previously triggered Gunyah PAGE_FAULT |
| GUI / Openbox / raw GLES viewer |960x540/30fps preset; X11 OpenGL clients use the existing GPU transport | CPU capture/copies remain; viewer fps differs from offscreen render fps |
| Weston composition |512MiB manual low-resolution GL compositor startup and Xwayland initialization | Short sample, not a heavy-desktop stability guarantee |
| Guest TUN, IPv4/IPv6 TCP | Normal Python TLS socket and proxy-free `curl` return HTTP200 | Outgoing only,12 tunnel sessions /16 broker connections |
| UDP / DNS | Real UDP DNS transaction/response ID and hostname resolution | UDP encapsulated over TCP/vsock; not arbitrary IP or Ethernet |
| Ping | Real IPv4 ICMP relay reply | Explicit `plb-ping` / PATH alias; TUN synthetic ping replies disabled |
| Network identity | Terminal or explicitly selected Bridge UID | Each app's Android VPN/background policy applies; no policy bypass/fallback |

The existing stock profile remains unchanged in design: it uses its original disk, Microdroid and proxy. The experimental profile has an independent disk. Networking is opt-in. DNS uses1.1.1.1/8.8.8.8 via the selected UID, and only a temporary bind mount replaces the guest resolver while the tunnel runs. Disabling and recreating the VM removes the interface/routes/mount.

Android reported `APP_BACKGROUND` blocking Terminal while Bridge was foreground. Both the old proxy and new TUN failed under that restriction; bringing Terminal forward restored DNS/TCP/ping without changing settings. Explicitly selecting Bridge enabled the same traffic with the Bridge GUI in front. This is why the identity is selectable, not silently switched after a failure. Background continuation does not grant network exemption.

## Direct virtio-gpu probes, not production launch flags

First, diskless VMs with `backend=2d` and `backend=virglrenderer` initialized the guest DRM driver. Both reported the host display as unsupported. VirGL exposed four capsets, including empty-size entries; this does not prove usable Vulkan.

A second diagnostic used the experimental disk **read-only** (`ro=true`, ext4 `ro,noload`), volatile guest `/run`, `/tmp` and `/dev`, and bounded EGL/Vulkan tools. With VirGL and default Vulkan-disabled settings, the guest EGL tool reported `virgl (Adreno (TM) 840)` / ES3.2 and exited0. With the correct Debian `virtio_icd.json`, Vulkan instance enumeration still found no usable physical GPU. Enabling the VMM's Vulkan flag produced capset/context failures and no working EGL/Vulkan device. The separate gfxstream probe aborted during host Vulkan initialization (`vkCreateInstance not found`). It was the owned diagnostic VMM, not Android, that exited.

The experimental and original qcow2 files had identical SHA256 before/after these read-only probes. The probe requests its own guest poweroff and never reboots Android. Neither a library/version listing nor VMM exit0 alone is counted as rendering success.

These results justify more work on render-server isolation, compatible Vulkan loader/backend, guest capsets, memory sharing and display transport. They do **not** justify a Vulkan, zero-copy or hardware-video switch. Production continues to use the separately unprivileged VTest renderer and authenticated RFB viewer. The direct crosvm decoder would need its own privilege review before exposing guest workloads to it.

## USB

The custom kernel has USB/xHCI/HID/storage/audio drivers, but no USB controller appeared in the protected guest PCI tree. The installed `crosvm usb list` returned an I/O error and the VMM logged a closed USB control pipe. This matches the crosvm implementation explicitly setting `cfg.usb=false` for protected VM types. Adding `--usb` is not a supported inverse option.

Protection was not disabled to get around this. Direct device assignment is unavailable in this build. USB-over-IP or a selected user-space USB protocol bridge would be separate work with guest-driver, authorization and exclusive-ownership requirements; neither is advertised as implemented. Android audio and keyboard/mouse forwarding remain separate integrations. No physical USB/Bluetooth peripheral was available, so hardware operation is unverified.

## Diagnostics and bounds

- `tools/build_gpu_probe.py` builds only a diagnostic initramfs; it does not launch a VM. The diagnostic C program refuses non-PID1 execution. Use only a stopped, explicitly selected experimental disk read-only and an isolated owned VMM, never simultaneous writers.
- `native/plb-network` is **guest-only**. Never execute it through Android root shell.
- Native transport suite:37 tests, including real TCP half-close and UDP binary/empty/large frames, malformed records and UDP flow exhaustion. Device policy probe:43 checks, zero failures. Untrusted Android UID rejected for all10 manager operations.
- Guest `/tmp/plb-network.log` contains lifecycle/errors; private host `files/vm/network-proxy.log` records broker diagnostics for either profile. No payload or clipboard/audio content is logged.
- Storage remained about46GiB free on the development Mac. Added source/build caches were tens of MiB; no kernel rebuild or expanded kernel checkout was needed.

Primary references: [Hev tunnel at the pinned revision](https://github.com/heiher/hev-socks5-tunnel/tree/780b396b0fc3d0355fc0614e221e3446933669c4), [UDP-in-TCP framing](https://github.com/heiher/hev-socks5-core/blob/9f1cf0bfca0c003dd0cf0fbdcf6912a2b46a8d05/README.md#udp-in-tcp), [crosvm protected-USB branch](https://chromium.googlesource.com/crosvm/crosvm/+/1c797bf2b462970f788e6e975ba137e917274665/src/crosvm/cmdline.rs), [crosvm USB interface](https://crosvm.dev/book/devices/usb.html). Upstream source explains a limitation; device observations above establish what happened on this build.
