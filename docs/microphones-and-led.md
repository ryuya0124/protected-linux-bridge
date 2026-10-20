# Debian microphones and VM LED control

These app controls currently apply to the Debian bridge. They are not Android
guest audio/camera HAL adapters.

## Microphones

The Devices audio detail shows the number of Android built-in input ports and
OS microphone descriptors. These are separate concepts: HAL processing can
combine physical capsules, and a descriptor inventory is not proof that every
physical microphone is independently accessible. Audio is still 48 kHz signed
16-bit **mono** into Debian's PulseAudio source.

Every exposed built-in input is listed. Multiple indistinguishable defaults
refuse capture rather than silently choosing the first. Persisted choices use
type/address/product name; transient device IDs appear only in the display
label. Duplicate stable identities remain ambiguous and do not gain an unsafe
index-based fallback. Removal or routing mismatch stops transfer. Before each
PCM block, the actual recording route must equal the selected device.

The direction setting uses Android's microphone-direction preference. A rejected
non-default direction fails clearly; it never pretends to select a particular
physical capsule. Active microphone descriptor counts are queried only after
recording starts. Inventory alone never opens an AudioRecord or asks for capture.

References: [AudioManager.getMicrophones](https://developer.android.com/reference/android/media/AudioManager#getMicrophones()),
[AudioRecord microphone direction](https://developer.android.com/reference/android/media/AudioRecord#setPreferredMicrophoneDirection(int)).

## LED from the Debian shell

In Devices → Audio, explicitly choose a flash-capable camera and enable **Allow
visible Debian sessions to control LED**. Both permissions are off/unselected by
default. Open a Bridge terminal or desktop, then use:

```sh
plb-torch status
plb-torch on            # default level, 10 seconds
plb-torch on 2 5        # device-supported level, 5 seconds
plb-torch off
```

The maximum lease is 30 seconds. No root camera operation or CameraDevice open
is used. The normal CameraManager API checks device/service availability; API
failures remain errors. Closing/pausing the visible session, transport loss or
consent revocation closes the lease and attempts OFF. Transient OFF failures get
five bounded attempts and an explicit `VM_TORCH_OFF_FAILED` error if all fail.
No software can guarantee physical OFF when the camera service itself refuses.

An already-on light not owned by this session is refused. CameraManager provides
**no exclusive ownership**; another app can still change or reassert the same
light while a lease exists. The bridge cannot perfectly identify that takeover
and must not promise complete isolation from another torch app.

The module-UID-only channel cannot start a VM. A guest-root-only UNIX broker
allows one Bridge connection and one outstanding fixed 4-byte request/8-byte
reply with bounded timeouts. It transports no host command, filesystem path or
camera identifier. Host main-loop work is limited to one in-flight request;
malformed/overlapping input disconnects. Standard Terminal's different UID does
not receive the Bridge app's hardware privileges. Use Bridge's own visible
terminal/desktop for LED control; background and Android guest HAL forwarding
are not implemented.

Reference: [CameraManager torch API and ownership limits](https://developer.android.com/reference/android/hardware/camera2/CameraManager#setTorchMode(java.lang.String,%20boolean)).
