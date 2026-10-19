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

## Surface prerequisite measured; 60 fps remains the destination

The isolated `tools/surface_probe` app and root renderer now pass a real
cross-process test. The root process obtains an app-owned Surface through a
root-only ContentProvider Binder call, converts it to an `ANativeWindow` in
that same process and uses host Adreno 840 EGL/GLES. It completed 90 swaps in a
three-second, deliberately paced test. A separate PixelCopy verification from
the app returned `SUCCESS` and the final expected blue pixel (`ff0000ff`).
PixelCopy is only the test assertion, not the presentation transport. The
permission-free, non-debuggable probe app was removed after the test.

This verifies the Surface ownership/process boundary and hardware presentation
prerequisite. It does **not** connect guest gfxstream output, measure VM frame
rate, establish zero-copy buffer sharing, or validate a 60 fps session.

The requested final target is **60 actually presented frames per second**.
Implementation proceeds through these separately verified steps:

1. Retain the validated raw/PNG fallback and its 10 capture requests/s ceiling.
   Later idle five-second windows reached about 9.8 fps; interactive observations
   remain around 3–6 fps. These are different workloads, not a universal rate.
2. Connect the actual gfxstream renderer to the app Surface in its owning
   process, keeping the existing VMM/kernel/disk profile available for rollback.
   The Surface prerequisite above passes; this guest-output step is incomplete.
3. Establish stable continuous presentation before increasing cadence: first
   verify a bounded 30 fps workload, then target 60 fps with buffer reuse and
   real acquire/release synchronization. Bound the queue rather than accumulating
   frames and latency. Handle Surface replacement, backgrounding and rotation.
4. Measure producer, submitted and presented frame counts separately, plus
   frame-interval percentiles, missing/late frames and input-to-display latency.
   A 60 Hz timer, swap-call count or GPU renderer name alone is not a 60 fps result.
5. Replace delayed swipe replay with a verified streaming input path if needed.
   The current input path sends a bounded swipe after finger-up; presentation
   optimization alone cannot remove that input limitation.

No phase permits changes to the host kernel, SELinux, Gunyah protection or boot
partitions. Direct presentation remains experimental until the actual VM output
and lifecycle tests pass. The fallback stays available on incompatibility.

## Background lifetime and shutdown diagnostics

A blocked app-side log reader exposed a separate lifecycle issue: VMM output
could block in `pipe_write`, and closing the early-console reader before guest
shutdown produced repeated broken-pipe diagnostics. The root manager now drains
VMM output into its own bounded 2 MiB private log independently of the app reader.
It detects the page-fault marker across chunk boundaries. File-write failure
continues draining instead of blocking the VM. No file-size limit is imposed
on the VMM or its qcow2 disks. The console reader stays alive through guest
shutdown and is joined after the owned child exits.

A regression test fills the UI pipe, feeds more than the private log's capacity
and splits the fault marker across writes. It verifies bounded logging and
fault detection without reading the UI pipe. This change does not exempt the
VM from the 30-minute lifetime or host health/RAM guards.


## Native-window VM trials, 2026-10-20

The developer Surface VMM is a separately built `cdylib` from the pinned crosvm
revision. An explicitly started root `app_process` obtains the test app's
Binder-transferred Surface, validates the pinned library by file descriptor,
and invokes the unchanged protected-VM arguments through JNI in that same
process. The prepared 4 GiB memory contract, protected LEND, 128 MiB in-guest
I/O pool, six-key guardian and host health checks remain in force. The original
executable and guest disks remain available; writable files are isolated in an
offline-verified `cf17-gpu4096-surface` clone.

Attaching a native window alone did not establish guest presentation. The
pinned crosvm only dispatched `Rutabaga::resource_flush` directly on Windows;
Android followed the generic headless display path. The Surface-only patch
adds that dispatch only when the Android process owns an attached native
window. The normal headless APK's behavior is unchanged. The renderer API and
Android native-window behavior are traced to the pinned source and AOSP's
[gfxstream frontend](https://android.googlesource.com/platform/hardware/google/gfxstream/+/refs/heads/main/host/virtio-gpu-gfxstream-renderer.cpp).

Trial 1 stopped early because an unlock command before the guest window service
was ready closed the test session. Trial 2 kept control alive, reached BOOT=1
and focused the Android launcher. The user observed guest output; after reopening
the viewer it became black. Initial PixelCopy and the later host screenshot were
black, while guest `screencap` contained the Android home. Thus continuous,
reopen-safe guest presentation and its frame rate were not established by that
trial. Its normal guest shutdown and both policy restorers completed.

The next test retains a SurfaceTexture/producer queue independently of Activity
recreation and attaches it to the new TextureView, avoiding a native-window
pointer to a destroyed SurfaceView queue. This is a GPU texture/composition
path, not a CPU screenshot playback path, but zero-copy and actual cadence must
still be measured. Trial 3 failed during guest startup with Gunyah page-fault
`attempt=-12`, before this lifecycle change could be evaluated; it is not a
successful rendering result. Host taint remained 4608, SELinux Enforcing and all
six policy values returned to `never` after these trials. The probe is bounded
to three minutes and is not enabled in the normal APK.


Trial 4 then completed guest startup in the retained-texture variant. PixelCopy
of the actual renderer target returned a 371,517-byte Android wallpaper/navigation
image, followed by a 374,091-byte Android launcher image after host Home and
reopening the viewer. Both were visually inspected; the user independently
confirmed output and reopen recovery. The control client sent no raw/PNG frame
requests: PixelCopy was a diagnostic assertion only. This establishes real guest
GPU output into the app's texture queue, including one two-second background /
reopen cycle. It does not establish arbitrary process-death recovery, fullscreen
rotation handling, long-term stability, zero-copy or 60 fps. The separate test
window still expires after three minutes; that expiry is not a rendering fault.
