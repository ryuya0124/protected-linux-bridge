# Android 17 peripheral work

Status: developer work, not working physical-device forwarding in the released
APK. Host kernel/modules/partitions/protections are unchanged. Tests described
below use no host Activity, Surface, screenshots or input. Guest offscreen UI
may execute inside its owned VM. Camera and microphone capture were not started.

## Measured host source and protocol

On the inspected Galaxy, the app-level inventory finds accelerometer, gyroscope,
magnetic field, proximity, light and pressure. Temperature/humidity are not
advertised when absent. The source explicitly selects sensors, limits actual
forwarding to 50 Hz, retains Android natural-device axes/units and stops its
callback stream on close. A three-second accelerometer test delivered 116
forwarded events with no source errors or events after close. Initial vendor
callbacks arrived faster than the requested period, so the source now applies
its own timestamp-based limit. No body/activity/vendor-private sensors are read.

`native/android_guest_sensors.h` implements Goldfish/Cuttlefish framed CONTROL
and DATA messages, available/enabled masks, bounded rate, guest-clock conversion,
finite-value validation and revocation. The transport defaults to an empty
sensor set and requires explicit owner configuration. Its control name for
magnetic field is `magnetic-field`, while DATA uses `magnetic`; these are distinct
in the actual AOSP implementation. Tests cover that distinction, response bytes,
unselected sensors, missing clocks, stale/duplicate samples, invalid floats,
capacity bounds and reset. App-source-to-owned-controller IPC still needs wiring.

The host inventory detects audio inputs/outputs and four logical camera IDs.
This is inventory, not guest forwarding. Camera2 source code selects a supported
YUV size and 15/30 fps range, owns at most two images, drops old frames, and
closes its session/device/reader on stop. It requires both session sharing
consent and Android CAMERA permission. The actual-device denial test passed
without opening a camera. Successful physical capture, rotation handling and
image delivery into the guest camera HAL remain unverified. No automatic camera
permission prompt or capture was added.

Existing `AudioBridge` uses ordinary app AudioTrack/AudioRecord and explicit
microphone lifecycle/permissions for Debian. It is not yet an Android guest HAL
adapter. Root capture is not a substitute for that permission path.

## VM-side sound device

A separate private source VMM, built from the same pinned source/overlays with
the additional `audio` feature, enumerated a virtio-snd card in the custom guest
kernel. A 512 MiB diskless VM completed its nonce-bound 16 MiB full readback,
reported `card_present=1`, powered off and returned controller0. The backend was
`null`, capture disabled and input devices/streams zero. No host audio stream
was opened. The first attempt used an incorrect CLI spelling and failed before
VM boot; the verified source's actual option is `--virtio-snd`.

This validates the guest kernel/device path, not Android application audio or
physical microphone/speaker transport. The separate build keeps the baseline
VMM unchanged. The custom guest kernel already has CONFIG_SND_VIRTIO=y.

## Exact Android guest HAL inventory

A later 1 GiB Android 17 session booted without any host Activity or Surface.
Every backing page passed the all-THP pre-handoff gate; BOOT=1 and Adreno GLES
were observed. Fixed, identity-checked read-only guest queries established:

- Audio AIDL IConfig, default/bluetooth/r_submix IModule and effect IFactory exist.
- Audio policy reports its configuration source as AIDL HAL and lists virtual
  Speaker/Built-In Mic ports, despite `/proc/asound/cards` having no card in the
  normal no-audio-device profile. Port names do not prove physical transport.
- Camera AIDL provider internal/0 exists, with three emulated devices, two normal
  cameras and no active camera clients. These are not the host's physical cameras.
- Sensor AIDL service exists but reports no sensors under the default empty mask.
- The remaining audio.primary.default.so file does not prove that HIDL library
  is the active path; AIDL service/configuration evidence takes precedence.

Guest shutdown disconnected its ADB service, so the helper did not receive a
positive shutdown reply. Independent VMM0/controller0 shutdown markers and clean
postflight establish normal session termination. No physical media was opened.
The earlier admission attempt stopped before VMM exec on an incomplete resident
page observation. The final scan now uses the same single bounded PTE refresh
as the existing per-batch observer; it still requires complete visible THP backing
and never reads live donated VMM memory. The later session passed the full gate.

## Sources and next connection work

Primary protocol references:

- [Goldfish sensor commands/data parser](https://android.googlesource.com/device/generic/goldfish/+/refs/heads/android17-release/hals/sensors/multihal_sensors_qemu.cpp)
- [Goldfish sensor IDs/names](https://android.googlesource.com/device/generic/goldfish/+/refs/heads/android17-release/hals/sensors/sensor_list.cpp)
- [Cuttlefish sensor framing/proxy](https://android.googlesource.com/device/google/cuttlefish/+/refs/heads/android17-release/host/commands/sensors_simulator/sensors_hal_proxy.cpp)
- [crosvm sound parameters](https://android.googlesource.com/platform/external/crosvm/+/c7dd8d6950912fa5e6fde7ec2d1616e0ddc090f8/devices/src/virtio/snd/parameters.rs)

Connect the already sampled sensors to the owned controller and verify events
in the guest SensorManager. Then attach the null-backed sound device to the
Android profile and inspect its AIDL HAL/ALSA route before any physical audio.
Camera requires a verified injection path into the guest's existing provider or
a separate guest-only HAL; a Camera2 host source alone does not provide it.
Keep per-resource opt-in, app permissions, bounded authenticated session IPC,
no automatic capture and production readiness false until qualified.
