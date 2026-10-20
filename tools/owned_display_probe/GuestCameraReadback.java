package net.ryuya_dev.protectedlinux.guestprobe;

import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

/** Guest-only synthetic camera. Never opens a host camera or saves frames. */
public final class GuestCameraReadback {
  public static void main(String[] arguments) throws Exception {
    System.out.println("PLB_GUEST_CAMERA_STAGE java_started");
    Looper.prepareMainLooper();
    Object activityThread =
        Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
    Context context =
        (Context) activityThread.getClass().getMethod("getSystemContext").invoke(activityThread);
    CameraManager manager = context.getSystemService(CameraManager.class);
    // This exact ID is created by the built-in guest-only service command.
    // Do not fall back to any physical/emulated camera if registration failed.
    if (!Arrays.asList(manager.getCameraIdList()).contains("9987"))
      throw new IllegalStateException("Owned synthetic guest camera missing");
    HandlerThread thread = new HandlerThread("guest-camera-readback");
    thread.start();
    Handler handler = new Handler(thread.getLooper());
    AtomicBoolean closed = new AtomicBoolean();
    AtomicReference<CameraDevice> camera = new AtomicReference<>();
    AtomicReference<CameraCaptureSession> session = new AtomicReference<>();
    AtomicReference<String> failure = new AtomicReference<>();
    AtomicInteger frames = new AtomicInteger(), invalid = new AtomicInteger();
    AtomicLong last = new AtomicLong();
    CountDownLatch received = new CountDownLatch(3);
    ImageReader reader = ImageReader.newInstance(640, 480, ImageFormat.YUV_420_888, 2);
    reader.setOnImageAvailableListener(
        available -> {
          if (closed.get()) return;
          try (Image image = available.acquireLatestImage()) {
            if (image == null || closed.get()) return;
            if (image.getWidth() != 640
                || image.getHeight() != 480
                || image.getPlanes().length != 3
                || image.getTimestamp() <= last.get()) invalid.incrementAndGet();
            last.set(image.getTimestamp());
            frames.incrementAndGet();
            received.countDown();
          }
        },
        handler);
    boolean complete;
    try {
      manager.openCamera(
          "9987",
          new CameraDevice.StateCallback() {
            @Override
            public void onOpened(CameraDevice device) {
              if (closed.get()) {
                device.close();
                return;
              }
              camera.set(device);
              try {
                CaptureRequest.Builder request =
                    device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
                request.addTarget(reader.getSurface());
                device.createCaptureSession(
                    List.of(reader.getSurface()),
                    new CameraCaptureSession.StateCallback() {
                      @Override
                      public void onConfigured(CameraCaptureSession configured) {
                        if (closed.get()) {
                          configured.close();
                          return;
                        }
                        session.set(configured);
                        try {
                          configured.setRepeatingRequest(request.build(), null, handler);
                        } catch (Exception error) {
                          failure.set(error.getClass().getSimpleName());
                        }
                      }

                      @Override
                      public void onConfigureFailed(CameraCaptureSession configured) {
                        configured.close();
                        failure.set("Guest session configuration failed");
                      }
                    },
                    handler);
              } catch (Exception error) {
                failure.set(error.getClass().getSimpleName());
              }
            }

            @Override
            public void onDisconnected(CameraDevice device) {
              device.close();
              failure.set("Guest camera disconnected");
            }

            @Override
            public void onError(CameraDevice device, int error) {
              device.close();
              failure.set("Guest camera error " + error);
            }
          },
          handler);
      complete = received.await(5, TimeUnit.SECONDS);
    } finally {
      closed.set(true);
      CountDownLatch stopped = new CountDownLatch(1);
      handler.post(
          () -> {
            try {
              CameraCaptureSession configured = session.get();
              if (configured != null) configured.close();
              CameraDevice device = camera.get();
              if (device != null) device.close();
              reader.close();
            } finally {
              stopped.countDown();
            }
          });
      if (!stopped.await(2, TimeUnit.SECONDS)) failure.set("Guest camera close deadline");
      thread.quitSafely();
      thread.join(1000);
    }
    System.out.println(
        "PLB_GUEST_CAMERA_READBACK synthetic=true frames="
            + frames.get()
            + " invalid="
            + invalid.get()
            + " error="
            + failure.get());
    boolean passed = complete && frames.get() >= 3 && invalid.get() == 0 && failure.get() == null;
    System.out.println(passed ? "PLB_GUEST_CAMERA_HAL_PASS" : "PLB_GUEST_CAMERA_HAL_FAILED");
    System.exit(passed ? 0 : 1);
  }
}
