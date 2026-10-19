# Android guest display transport

## Implemented: raw capture instead of PNG plus a fixed wait

Preview.8 initially used one `screencap -p` over the fixed guest-vsock ADB
endpoint per frame, then slept for 1500 ms. It decoded a PNG and allocated a new
Bitmap for every frame. Input arriving during the sleep waited for the next
cycle, and capture/transfer added more time to the interval.

The revised viewer requests uncompressed `screencap`. The native side and Java
side independently require exactly 480×800, RGBA_8888, sRGB, a 16-byte header
and 1,536,000 pixel bytes. No arbitrary dimensions/stride/format are accepted.
Unsupported raw output triggers the existing validated PNG path. A single
bounded byte buffer and mutable UI-thread Bitmap are reused; the worker waits
for the UI copy before reusing bytes. Capture requests are capped at 10/s.
An input-aware queue wait replaces the fixed sleep; queued input is serviced
before another capture. Backgrounded viewers ping without capturing frames.

The source layout is documented by AOSP's
[screencap writer](https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/cmds/screencap/screencap.cpp):
width, height, pixel format and color-space integers, followed by packed rows.
The selected running guest returned `[480,800,1,1]` and 1,536,016 bytes.

On-device app telemetry over eleven five-second foreground windows measured
15–31 displayed frames per window, approximately **3–6 fps**. No PNG fallback
was logged in those windows. A separate Mac/root/base64 comparison measured
PNG acquisition at 0.73–1.11 s and raw acquisition at 0.56–0.58 s; those timings
include the diagnostic transport and are **not** isolated GPU/capture timings.
The raw and PNG files in that comparison showed different apps because the
user was interacting with the guest, so no pixel-identical comparison is claimed.
The raw decoded image was visually inspected with correct-looking colors.

183 Python/native tests, Java gesture/frame boundary tests, Release Lint and
the signed release build passed. The updated APK was installed with existing
data retained, and the app-owned 4 GiB guest reached the raw viewer. Gunyah
startup reliability remains independent and unresolved; another user-initiated
launch failed before the successful observed session. No automatic retry,
host reboot, kernel/protection change or extra THP setting was introduced.

## Next boundary: real GPU output to the app Surface

Raw capture still reads pixels back from guest SurfaceFlinger and copies them
through vsock and a host Bitmap. It is **not** direct GPU presentation.

Read-only inspection of the exact pinned host gfxstream library found exported
`gfxstream_backend_setup_window` and `stream_renderer_export_blob`. The pinned
crosvm source's Windows surface backend already declares the former with seven
arguments. The public gfxstream
[window entry point](https://android.googlesource.com/platform/hardware/google/gfxstream/+/refs/heads/emu-35-1-dev/host/virtio-gpu-gfxstream-renderer.cpp)
forwards a native window to its renderer; its Android
[native subwindow implementation](https://android.googlesource.com/platform/hardware/google/gfxstream/+/refs/heads/main/host/NativeSubWindow_android.cpp)
acquires an `ANativeWindow` and sets RGBA buffer geometry. Export presence and
upstream examples alone do not verify the exact installed binary's behavior.

The existing custom VMM deliberately uses a headless display. The pinned Android
display-client stub is unimplemented; its generic Android surface wrapper uses
a CPU framebuffer callback. Merely selecting that backend or calling its stub
would not create a working direct GPU path. Likewise, a DMA-BUF export function
does not prove the current guest ColorBuffer exports an Android-importable
hardware buffer with matching metadata and synchronization.

The next experiment must verify an app-owned Surface's cross-process lifetime
and rendering in the actual gfxstream process, or an actual export/importable
GPU-buffer path with acquire/release fences. A pointer obtained in the app
cannot be passed as a pointer to another process. Peer UID/session authentication,
rotation/Surface destruction, renderer lifetime and fail-safe return to raw
capture are required. Keep the measured VMM/kernel/profile available separately
and do not weaken SELinux or Gunyah protections to force this path to work.
