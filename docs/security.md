# Protection boundaries and remaining risks

This experimental bridge does not provide the full security properties of a verified AVF deployment. It uses the device's existing Gunyah crosvm and existing Microdroid kernel with `--protected-vm-without-firmware`. There is no pvmfw verified-boot chain or attestation. No kernel, boot partition or SELinux setting is changed.

## Boundaries present

- Linux runs a distinct kernel instance. Debian root/sudo is guest privilege, not Android root.
- No Android filesystem sharing, physical GPU device passthrough or virtual NIC is exposed. Only the owned disk, serial and bounded vsock services are attached.
- The root manager authenticates Android callers by SO_PEERCRED: Terminal-only PTYs; Terminal/module GUI; root/module query/stop. Clients require a UID0 server. Guest services accept host CID2 only.
- Network egress drops to the Terminal UID with its existing inet group and zero effective capabilities/NoNewPrivs; destination restrictions and session/time/buffer caps apply. This still consumes host network resources and follows Android app/VPN policy.
- GPU command decoding runs separately as UID/GID9999, no supplementary groups/effective capabilities, NoNewPrivs. The root manager relays bounded opaque VTest traffic. This reduces decoder privilege but does not guarantee a safe GPU driver.
- Root-owned mode0700 storage contains the private copied base/qcow2. The stock Linux image is not modified. CPU/memory/time/session limits bound resource use, not all possible denial of service.

## Crucial remaining limitation

The host VMM currently runs as root with `--disable-sandbox`. Guest-triggered VMM vulnerabilities can therefore affect Android. The hypervisor, host kernel, GPU driver, root manager and terminal/display parsers remain trusted code. Root launching is not made harmless merely by using a protected guest.

On the tested October18 Android16 build, a minimal owned no-disk/no-vsock sandbox probe failed with `failed to find embedded seccomp policy: serial_device`. The running host configuration has USER_NS and SECURITY_LANDLOCK disabled. These observations are device/build-specific; they do not prove that every possible sandbox is impossible. They do mean that simply removing `--disable-sandbox` or using Landlock does not work here. We did not weaken SELinux or update any kernel to bypass this. Production VMM flags were not changed by that probe.

Do not run untrusted hostile guest images/code expecting isolation equivalent to standard verified AVF. Keep bridge/GPU dependencies updated, use only needed features, and stop the owned VM after use. Consult [AOSP AVF security](https://source.android.com/docs/core/virtualization/security) for the verified platform model; the guarantees described there must not be attributed to this custom path.
