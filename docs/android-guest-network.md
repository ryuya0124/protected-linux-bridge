# Android guest internet

The separate Android 17 / 4096 MiB GPU profile can optionally use an ordinary
Android network inside the guest. Legacy profiles remain unchanged. The launch
dialog exposes the option; disabling it starts the VM offline. The viewer reports
configuration failure separately, so a failed relay does not hide the guest UI.

## Data path and boundaries

Guest applications use Android's normal networking APIs and default network.
A guest-only TUN (`plb0`) feeds the pinned HEV tun2socks implementation. A loopback
broker forwards SOCKS streams over vsock to a relay accepting only the selected
guest CID on a distinct port. The host relay drops to the Bridge app's UID,
keeps only Android's ordinary `inet` group, has zero effective capabilities and
sets `NO_NEW_PRIVS`. Android routes its traffic as traffic from Bridge, including
any host per-app VPN policy that applies to Bridge.

A guest root `NetworkAgent` registers the TUN's address, route and DNS with
ConnectivityService. Android/netd owns the guest's per-network routing and DNS.
There is no host TUN, VPN service, route, resolver, firewall or SELinux change.
Guest applications do not need to specify a proxy or join any Xposed scope.
The two explicit DNS resolvers are 1.1.1.1 and 8.8.8.8. The initial implementation
covers IPv4 TCP and UDP; it does not promise IPv6, raw sockets, broadcast, LAN
service discovery or arbitrary ICMP. DNS/TLS validation is not bypassed.

The implementation checks the exact guest device/hardware/build and root UID
before every upload/setup/stop command. Build-time fixed payloads have SHA-256
checks before execution, fixed private paths and bounded sizes. There is no
caller-supplied command or upload endpoint. Framework stubs contain only the
actual guest signatures and are compile-only: they are excluded from the DEX.
Unsupported signatures fail setup, rather than modifying Android policy.

The optional controller belongs to the VM manager and cleans up only its own
relay. The relay closes inherited RAM, owner-lock, control and disk descriptors
before dropping privilege. Its file-size limit applies only after that boundary,
to its own diagnostic log, never to the VMM/disks. The root controller retains the validated shared owner-lock descriptor until
guest cleanup finishes, preventing a replacement VM from reusing the fixed CID
during cleanup after a manager exit. Guest cleanup checks the guest boot ID and
recorded process birth times before signaling its children. Closing the viewer stops the
owned network controller and guest; Debian and unrelated VMs are not involved.

## Evidence and lifecycle checks

A running app-owned 4 GiB Android guest established this path without changing
host networking. ConnectivityService selected it as the active default network
and reported `VALIDATED` after real Android validation probes. Private DNS also
validated the supplied resolvers. The guest's ordinary WebView Browser Tester
then opened `https://example.com/` and rendered the HTTPS document. No app-specific
proxy configuration, fake validation capability or TLS-error override was used.
The host relay's effective UID/GID matched Bridge, group list contained only
`inet`, capabilities were zero, `NO_NEW_PRIVS` was set and its descriptors were
only stdin/stdout `/dev/null`, its log and listening socket.

The disposable descriptor-boundary fixture also runs on the selected Android
host: it closes VM-like descriptors in a forked child, retains its listener,
and verifies the parent descriptors remain open. The Mac test instead verifies
fail-closed behavior when `/proc/self/fd` is unavailable.

The first detached guest launch died when its ADB exec session closed. The
fixed launch creates a new session, retains no ADB stdio and checks readiness.
The real network result came after this correction. Initial app integration,
stop/relaunch and failure-path checks are recorded separately as they finish;
the live developer experiment alone is not proof of every release lifecycle.


A signed app integration trial also enabled the network after the viewer's
readiness handshake. Its separate status reported setup success, and Android
again selected a genuinely validated default network and rendered the HTTPS
page. After guest power-off the owned host controller and relay exited. The
subsequent owner-lock/boot-ID hardening passes a regression test in which the
manager closes its lock while the child remains alive: another opener remains
excluded until child cleanup releases the shared open-file description.
An ordinary viewer-close-before-power-off check of that hardened build remains
separate from the earlier guest-powered-off test.

Aurora Store was installed through guest ADB in the existing 4 GiB profile and
is included in the separately verified offline Surface-test copy. No browser
APKs are bundled with Bridge. Chrome and Firefox installation attempts were
cancelled at the user's request; their two owned incomplete install sessions
were abandoned. Their downloads and installation-session logs are private.
