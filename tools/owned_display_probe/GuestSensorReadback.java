package net.ryuya_dev.protectedlinux.guestprobe;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/** Runs only inside the identity-checked guest. Reports counts, never physical values. */
public final class GuestSensorReadback {
  public static void main(String[] arguments) throws Exception {
    Looper.prepareMainLooper();
    Object activityThread =
        Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
    Context context =
        (Context) activityThread.getClass().getMethod("getSystemContext").invoke(activityThread);
    SensorManager manager = context.getSystemService(SensorManager.class);
    Sensor sensor = manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
    if (sensor == null) throw new IllegalStateException("Guest accelerometer missing");
    HandlerThread thread = new HandlerThread("guest-sensor-readback");
    thread.start();
    AtomicInteger events = new AtomicInteger(), invalid = new AtomicInteger();
    AtomicLong last = new AtomicLong();
    CountDownLatch received = new CountDownLatch(10);
    SensorEventListener listener =
        new SensorEventListener() {
          @Override
          public void onAccuracyChanged(Sensor sensor, int accuracy) {}

          @Override
          public void onSensorChanged(SensorEvent event) {
            boolean valid = event.values.length >= 3 && event.timestamp > last.get();
            for (int i = 0; i < Math.min(3, event.values.length); i++)
              valid &= Float.isFinite(event.values[i]);
            if (!valid) invalid.incrementAndGet();
            last.set(event.timestamp);
            events.incrementAndGet();
            received.countDown();
          }
        };
    boolean registered = false, complete = false;
    try {
      registered =
          manager.registerListener(listener, sensor, 50000, new Handler(thread.getLooper()));
      complete = registered && received.await(5, TimeUnit.SECONDS);
    } finally {
      manager.unregisterListener(listener);
      thread.quitSafely();
      thread.join(1000);
    }
    boolean passed = complete && events.get() >= 10 && invalid.get() == 0;
    System.out.println(
        "PLB_GUEST_SENSOR_READBACK events="
            + events.get()
            + " invalid="
            + invalid.get()
            + " registered="
            + registered);
    System.out.println(passed ? "PLB_GUEST_SENSOR_EVENTS_PASS" : "PLB_GUEST_SENSOR_EVENTS_FAILED");
    System.exit(passed ? 0 : 1);
  }
}
