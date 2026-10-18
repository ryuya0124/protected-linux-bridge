# Protection boundaries and remaining risks

This experimental bridge does not provide the full security properties of a verified AVF deployment. It uses the device's existing Gunyah crosvm and existing Microdroid kernel with `--protected-vm-without-firmware`. There is no pvmfw verified-boot chain or attestation. No host kernel, boot partition or SELinux setting is changed.

## Boundaries present

- Linux runs a distinct kernel instance. Debian root/sudo is guest privilege, not Android root.
- No Android filesystem sharing, physical GPU device passthrough or host virtual NIC is exposed. A guest-only TUN may be enabled in the experimental profile; it connects to bounded vsock services.
- The root manager authenticates Android callers by SO_PEERCRED: Terminal-only PTYs; Terminal/module GUI; root/module network-status/stop. Clients require a UID0 server. Guest services accept host CID2 only.
- Network egress drops to the explicitly selected Terminal/module UID (Terminal by default) with its inet group and zero effective capabilities/NoNewPrivs; destination restrictions and session/time/buffer caps apply. This still consumes host network resources and follows Android app/VPN policy.
- GPU command decoding runs separately as UID/GID9999, no supplementary groups/effective capabilities, NoNewPrivs. The root manager relays bounded opaque VTest traffic. This reduces decoder privilege but does not guarantee a safe GPU driver.
- Root-owned mode0700 storage contains the private copied base/qcow2. The stock Linux image is not modified. CPU/memory/time/session limits bound resource use, not all possible denial of service.

## Crucial remaining limitation

The host VMM currently runs as root with `--disable-sandbox`. Guest-triggered VMM vulnerabilities can therefore affect Android. The hypervisor, host kernel, GPU driver, root manager and terminal/display parsers remain trusted code. Root launching is not made harmless merely by using a protected guest.

On the tested October18 Android16 build, a minimal owned no-disk/no-vsock sandbox probe failed with `failed to find embedded seccomp policy: serial_device`. The running host configuration has USER_NS and SECURITY_LANDLOCK disabled. These observations are device/build-specific; they do not prove that every possible sandbox is impossible. They do mean that simply removing `--disable-sandbox` or using Landlock does not work here. We did not weaken SELinux or update any kernel to bypass this. Production VMM flags were not changed by that probe.

Do not run untrusted hostile guest images/code expecting isolation equivalent to standard verified AVF. Keep bridge/GPU dependencies updated, use only needed features, and stop the owned VM after use. Consult [AOSP AVF security](https://source.android.com/docs/core/virtualization/security) for the verified platform model; the guarantees described there must not be attributed to this custom path.

## Optional guest kernel and integrations (0.4)

Guest kernels may now be added; host kernel/module/partition/SELinux/Gunyah protection changes remain prohibited. A fixed SHA256-verified guest profile uses a separate disk copy, one shared ownership lock and no arbitrary host path/command API. Stock Microdroid and the original writable disk remain available. Guest boot success is not proof of host isolation or workload stability: heavier tests hit a Gunyah memory-provisioning fault, and the VMM still runs root without its standard sandbox.

The full guest kernel enables namespaces/seccomp; legacy Microdroid service compatibility deliberately disables unsupported guest sandbox directives. Neither mode currently guarantees every systemd/BPF confinement facility. This does not change Android SELinux. Network traffic remains under the selected app UID/policy; no host routing/DNS/firewall changes are made.

A/M IPC is module-only; ordinary shell is denied. Android permission and visible microphone foreground-service rules apply to background capture. Removing a selected audio device stops its stream; no global Bluetooth/SCO or pairing change is made. Preferred routing is best-effort Android policy, not device exclusivity.

Optional clipboard automation intentionally grants the root-launched, dropped-shell worker access using its pre-existing shell clipboard permission; it is not a new system permission grant. Only that exact tracked PID can bridge the clipboard through C/W. The worker needs private shell attribution, skips sensitive Android clips, bounds text/queues and stores no history. A malicious guest can supply clipboard text when reverse sharing is enabled; automatic sharing is off by default. Disable it when handling sensitive data. Android itself may display clipboard privacy prompts.

## Experimental TUN networking (preview.2)

Only the guest creates `plb0`, default routes and a temporary resolver bind mount. Nothing creates a host TUN, NAT rule, listener or route. The module requests ordinary INTERNET permission so it can be explicitly chosen as egress identity; it does not inherit root network privilege. There is no fallback to another UID when Android rejects traffic. VPN/background restrictions on the chosen UID remain effective. The default Terminal identity is retained across upgrades.

The existing CID-checked vsock listener carries SOCKS5 TCP and Hev UDP-in-TCP framing after credentials are dropped. The guest SOCKS listener binds loopback only, is enabled only with the opted-in custom profile and is not exposed on Android TCP. Every resolved destination uses the existing fail-closed loopback/host-address/link-local/multicast policy. UDP sockets are connected to pinned destinations, with at most8 destinations per association,16 concurrent broker connections and bounded datagrams, timeouts and lifetime. The TUN has12 sessions maximum. Unsupported commands/malformed frames close the connection; there is no arbitrary host command/path or raw-IP injection API. The new N status request is root/module-only and never starts a VM.

DNS goes to1.1.1.1/8.8.8.8 through the selected app network; this is disclosed in the UI. Guest mount/route cleanup targets only its own configuration and refuses an existing guest default route or interface. Like other guest integrations it is not a security boundary against guest root. TCP half-close, binary/empty UDP, malformed framing, destination rejection and flow limits are exercised with real sockets. Android shell UID2000 is rejected for all10 manager operations, and the existing43 destination/authority/ICMP policy checks pass.

Direct virtio-gpu experiments used diskless or read-only diagnostic guests, not production VMM flags. The installed root crosvm's direct renderer is not the separately dropped-UID VTest decoder; a successful EGL query alone is insufficient reason to expose that additional decoder in the app. No GPU/Vulkan passthrough or host protection modification is shipped.
