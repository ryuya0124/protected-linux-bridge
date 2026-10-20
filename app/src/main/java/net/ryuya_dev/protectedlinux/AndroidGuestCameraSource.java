package net.ryuya_dev.protectedlinux;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.OutputConfiguration;
import android.hardware.camera2.params.SessionConfiguration;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Range;
import android.util.Size;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BooleanSupplier;
import java.util.function.Consumer;

/** Selected Camera2 source for a future owned guest session; never stores images or uses root. */
final class AndroidGuestCameraSource implements AutoCloseable {
  private final Context context;
  private final BooleanSupplier sharingAllowed;
  private final Consumer<Image> frames;
  private final Consumer<Exception> failure;
  private final AtomicBoolean closed = new AtomicBoolean();
  private HandlerThread thread;
  private ImageReader reader;
  private CameraDevice camera;
  private CameraCaptureSession session;
  private boolean started;

  AndroidGuestCameraSource(
      Context context,
      BooleanSupplier sharingAllowed,
      Consumer<Image> frames,
      Consumer<Exception> failure) {
    this.context = context.getApplicationContext();
    this.sharingAllowed = sharingAllowed;
    this.frames = frames;
    this.failure = failure;
  }

  synchronized void start(String cameraId, Size size, int fps) throws CameraAccessException {
    if (started || closed.get()) throw new IllegalStateException("Camera source already used");
    if (!sharingAllowed.getAsBoolean()
        || context.checkSelfPermission(Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED)
      throw new SecurityException(
          "Camera sharing requires explicit session consent and permission");
    if (cameraId == null
        || size == null
        || size.getWidth() < 1
        || size.getHeight() < 1
        || size.getWidth() > 1920
        || size.getHeight() > 1080
        || (fps != 15 && fps != 30))
      throw new IllegalArgumentException("Bounded camera size/rate required");
    CameraManager manager = context.getSystemService(CameraManager.class);
    if (manager == null || !Arrays.asList(manager.getCameraIdList()).contains(cameraId))
      throw new IllegalArgumentException("Selected camera unavailable");
    CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);
    var formats = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
    Size[] sizes = formats == null ? null : formats.getOutputSizes(ImageFormat.YUV_420_888);
    if (sizes == null || !Arrays.asList(sizes).contains(size))
      throw new IllegalArgumentException("Selected camera format/size unavailable");
    Range<Integer>[] rates =
        characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
    Range<Integer> selected = null;
    if (rates != null)
      for (Range<Integer> rate : rates)
        if (rate.getUpper() == fps && (selected == null || rate.getLower() > selected.getLower()))
          selected = rate;
    if (selected == null) throw new IllegalArgumentException("Selected camera rate unavailable");
    Range<Integer> requestedRate = selected;
    started = true;
    thread = new HandlerThread("android-guest-camera");
    thread.start();
    Handler handler = new Handler(thread.getLooper());
    try {
      reader =
          ImageReader.newInstance(size.getWidth(), size.getHeight(), ImageFormat.YUV_420_888, 2);
      reader.setOnImageAvailableListener(
          source -> {
            try (Image image = source.acquireLatestImage()) {
              if (image == null || closed.get()) return;
              synchronized (this) {
                if (!sharingAllowed.getAsBoolean()) close();
                // The sink consumes this frame synchronously. Holding an Image
                // past the callback is forbidden; no unbounded frame queue.
                if (!closed.get()) frames.accept(image);
              }
            } catch (RuntimeException error) {
              fail(error);
            }
          },
          handler);
      manager.openCamera(
          cameraId,
          command -> handler.post(command),
          new CameraDevice.StateCallback() {
            @Override
            public void onOpened(CameraDevice device) {
              synchronized (AndroidGuestCameraSource.this) {
                if (closed.get() || !sharingAllowed.getAsBoolean()) {
                  device.close();
                  close();
                  return;
                }
                camera = device;
                try {
                  device.createCaptureSession(
                      new SessionConfiguration(
                          SessionConfiguration.SESSION_REGULAR,
                          List.of(new OutputConfiguration(reader.getSurface())),
                          command -> handler.post(command),
                          new CameraCaptureSession.StateCallback() {
                            @Override
                            public void onConfigured(CameraCaptureSession configured) {
                              synchronized (AndroidGuestCameraSource.this) {
                                if (closed.get() || !sharingAllowed.getAsBoolean()) {
                                  configured.close();
                                  close();
                                  return;
                                }
                                session = configured;
                                try {
                                  var request =
                                      device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
                                  request.addTarget(reader.getSurface());
                                  request.set(
                                      CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, requestedRate);
                                  configured.setRepeatingRequest(request.build(), null, handler);
                                } catch (CameraAccessException | RuntimeException error) {
                                  fail(error);
                                }
                              }
                            }

                            @Override
                            public void onConfigureFailed(CameraCaptureSession failed) {
                              failed.close();
                              fail(new IllegalStateException("Camera stream configuration failed"));
                            }
                          }));
                } catch (CameraAccessException | RuntimeException error) {
                  fail(error);
                }
              }
            }

            @Override
            public void onDisconnected(CameraDevice device) {
              device.close();
              fail(new IllegalStateException("Selected camera disconnected"));
            }

            @Override
            public void onError(CameraDevice device, int error) {
              device.close();
              fail(new IllegalStateException("Camera unavailable: " + error));
            }
          });
    } catch (CameraAccessException | RuntimeException error) {
      close();
      throw error;
    }
  }

  private void fail(Exception error) {
    if (closed.get()) return;
    close();
    failure.accept(error);
  }

  @Override
  public void close() {
    if (!closed.compareAndSet(false, true)) return;
    synchronized (this) {
      if (session != null) session.close();
      if (camera != null) camera.close();
      if (reader != null) reader.close();
      if (thread != null) thread.quitSafely();
      session = null;
      camera = null;
      reader = null;
    }
  }
}
