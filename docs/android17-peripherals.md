# Android 17 peripheral work

Status: developer diagnostics. Live accelerometer forwarding and the guest
AudioTrack-to-ALSA path passed; physical audio/camera forwarding is not in the
released APK. Host kernel/modules/partitions/protections are unchanged. Tests described
below use no host Activity, Surface, screenshots or input. Guest offscreen UI
may execute inside its owned VM. Host camera/microphone capture and physical speaker playback were not started.

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
capacity bounds and reset.

The developer controller now accepts an explicit sensor mode. Its inherited
private stdin pipe carries the original 40-byte display capability, followed by
a fixed version/mask header and fixed-size sensor frames. The controller forwards
only the original capability to the VMM publisher on a separate pipe, retaining
the existing Binder session/UID claim. It alone consumes the sensor stream. There
is no TCP listener, public command socket or arbitrary path/command field. Invalid
masks, axes, timestamps, reserved bytes and non-finite values are refused. EOF or
stream failure stops this owned session. App callbacks keep at most one reading
per kind and never wait for the pipe writer. Ordinary callers still expose no
sensors; this mode is not enabled automatically in the released APK.

A headless 1 GiB Android session forwarded **202 live host accelerometer samples**.
The guest SensorManager registered its accelerometer and received **10 events**
with finite values and increasing timestamps. Guest shutdown, VMM 0/controller 0
and the normal host-boundary checks passed. A later combined audio/sensor session
forwarded 252 samples and again passed 10 guest events. Other supported sensor
kinds have host inventory/protocol coverage but no end-to-end device qualification.

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

The separate build keeps the baseline VMM unchanged. The custom guest kernel
already has CONFIG_SND_VIRTIO=y.

The same separate VMM then booted Android 17 with virtio-snd. The guest AIDL audio
module exposed a real ALSA playback device. A silent AudioTrack test wrote
**153,600 bytes**, advanced its playback head to **28,672 frames**, and observed
**one open /dev/snd/pcmC0D0p descriptor**. Guest shutdown and VMM 0/controller 0
passed with clean host postflight. The backend remained null with no input
streams, so this is a connected Android audio HAL/virtio device path, **not
physical speaker playback or microphone capture**.

Two earlier audio checks failed because the test tried to read an optional PCM
proc debug file. CONFIG_SND_VERBOSE_PROCFS is unset in this guest kernel. An
uncaught Java exception caused the shell's misleading `Killed`/137 output; it
was not evidence of host memory exhaustion. The corrected check uses playback
progress and actual guest PCM descriptors. Those earlier sessions had VMM 0 but
controller 52 during teardown; retain them as failed sessions, not clean passes.
The controller now gives an explicitly observed zombie a bounded reap window;
live/unreadable tasks and actual memory-limit failures still fail closed.

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
positive shutdown reply. Independent VMM 0/controller 0 shutdown markers and clean
postflight establish normal session termination. No physical media was opened.
The earlier admission attempt stopped before VMM exec on an incomplete resident
page observation. The final scan now uses the same single bounded PTE refresh
as the existing per-batch observer; it still requires complete visible THP backing
and never reads live donated VMM memory. The later session passed the full gate.

## Guest virtual-camera connection and current blocker

The inspected guest contains its stock virtual-camera binary and disabled/lazy
init declaration. A non-waiting `cmd` lookup alone did not start it. The fixed,
identity-checked helper can explicitly start **only the guest's** existing
service, without changing host init, properties, files or any SELinux policy.
CameraService connected to its `virtual/0` provider and requested its camera list.

The first command failed with Binder FAILED_TRANSACTION while the service stayed
running. Guest logs identified a denied read/write transfer of an inherited
`/dev/pts/0` FD into the `virtual_camera` SELinux domain. Passing null stdin and
pipe stdout/stderr fixed that issue: the service's real help/registration command
then executed. Neither enforcement nor policy was modified.

Registration now reaches the stock hardware-requirement check and refuses with
**`GL_EXT_YUV_target not supported`**. The source explicitly requires this GLES
extension. The examined renderer source has header/third-party mentions, which
are not proof of an implemented/exported runtime capability. Do not fabricate an
extension string, bypass the requirement or blame physical-camera permission:
this check used entirely synthetic guest frames and failed before camera creation.
Other vendor-property read denials also appear, but their causal role in the
missing extension is unproven. This is the current camera blocker.

The final session again passed live sensor events and silent Android audio,
then shut down with VMM 0/controller 0 and clean host postflight. Its camera check
and overall instrumentation result failed; **no camera frames were received**.
The initial camera attempt had instead been refused before VMM exec when 24 of
262144 backing pages failed the all-THP requirement. These are distinct failures.

The checked-in GuestCameraReadback fixture is ready to verify three synthetic
640x480 YUV frames and revoke its test camera once a compatible rendering path
is available. It is not a physical-camera forwarding implementation. A future
adapter must either provide a real supported guest rendering path or use a
separate guest-only provider/conversion design, retaining explicit host capture
consent and ordinary Camera2 permissions. No physical host camera was opened.

## Sources and next connection work

Primary protocol references:

- [Goldfish sensor commands/data parser](https://android.googlesource.com/device/generic/goldfish/+/refs/heads/android17-release/hals/sensors/multihal_sensors_qemu.cpp)
- [Goldfish sensor IDs/names](https://android.googlesource.com/device/generic/goldfish/+/refs/heads/android17-release/hals/sensors/sensor_list.cpp)
- [Cuttlefish sensor framing/proxy](https://android.googlesource.com/device/google/cuttlefish/+/refs/heads/android17-release/host/commands/sensors_simulator/sensors_hal_proxy.cpp)
- [AOSP primary AIDL audio/ALSA stream](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/android17-release/audio/aidl/default/primary/StreamPrimary.cpp)
- [AOSP virtual-camera registration and requirements](https://android.googlesource.com/platform/frameworks/av/+/refs/heads/android17-release/services/camera/virtualcamera/VirtualCameraService.cc)
- [crosvm sound parameters](https://android.googlesource.com/platform/external/crosvm/+/c7dd8d6950912fa5e6fde7ec2d1616e0ddc090f8/devices/src/virtio/snd/parameters.rs)

Next connect the null audio backend to an authenticated app-owned PCM adapter,
retaining AudioTrack/AudioRecord permissions and lifecycle. Do not replace this
with root AAudio capture. Camera2 host capture also needs a real guest injection
adapter; a host Camera2 source alone does not provide it. The inspected guest
contains `/system/bin/virtual_camera` and its disabled/lazy init service, even
though it was absent from `service list` before startup. AOSP provides an
explicit default-device test-camera registration path, suitable for proving
the guest-side pipeline with synthetic frames before physical capture.
Keep per-resource opt-in, app permissions, bounded authenticated session IPC,
no automatic capture and production readiness false until qualified.

## Reproduce fixed guest HAL checks

These helpers build small, fixed guest-only Java payloads and native clients.
They do not start a VM, use host UI, open host hardware or change host policy.
Use the pinned Android 37 SDK/platform, build-tools36.1.0, NDK28.2 and JDK21 on
macOS; retain outputs under ignored artifacts. For example, from the repository:

```sh
python3 tools/build_guest_hal_checks.py \
  --sdk "$ANDROID_SDK_ROOT" \
  --java-bin "$JAVA_HOME/bin" \
  --output artifacts/guest-hal-checks
```

Each native helper checks the fixed Cuttlefish identity over the owned guest's
vsock ADB before running its embedded command. It accepts no caller-selected
command, payload, guest address or upload path. Java classes execute only in
the guest. Sensor checks record counts, not physical values. Audio writes
silence to the null backend. The camera check claims only its newly registered
synthetic test camera, uses bounded ImageReader capture and revokes it on exit.
A completion marker alone does not qualify a session: also require normal VM
exit, controller 0 and clean host postflight.

## Offscreen YUV follow-up

The [YUV-target investigation](android17-yuv-target.md) now proves native Adreno
support and eight positive cases through a separate patched translator, including
actual YUV planes and an out-of-gamut raw sampler. The original translator rejects
the same external framebuffer. This does not resolve guest camera registration:
the guest encoder and coherent writable ColorBuffer path remain unqualified.
The extension stays unadvertised and production readiness remains false.
