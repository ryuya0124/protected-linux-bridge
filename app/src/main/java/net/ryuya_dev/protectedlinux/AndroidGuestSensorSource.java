package net.ryuya_dev.protectedlinux;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Handler;
import android.os.HandlerThread;
import java.util.EnumMap;
import java.util.EnumSet;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

/** Opt-in source for one owned Android guest session. No root, sockets or automatic start. */
final class AndroidGuestSensorSource implements SensorEventListener, AutoCloseable {
  enum Kind {
    ACCELEROMETER(0, Sensor.TYPE_ACCELEROMETER, 3),
    GYROSCOPE(1, Sensor.TYPE_GYROSCOPE, 3),
    MAGNETIC_FIELD(2, Sensor.TYPE_MAGNETIC_FIELD, 3),
    TEMPERATURE(4, Sensor.TYPE_AMBIENT_TEMPERATURE, 1),
    PROXIMITY(5, Sensor.TYPE_PROXIMITY, 1),
    LIGHT(6, Sensor.TYPE_LIGHT, 1),
    PRESSURE(7, Sensor.TYPE_PRESSURE, 1),
    HUMIDITY(8, Sensor.TYPE_RELATIVE_HUMIDITY, 1);
    final int guestId, androidType, axes;

    Kind(int guestId, int androidType, int axes) {
      this.guestId = guestId;
      this.androidType = androidType;
      this.axes = axes;
    }
  }

  record Reading(Kind kind, long hostTimestampNs, float[] values) {
    Reading {
      values = values.clone();
    }

    @Override
    public float[] values() {
      return values.clone();
    }
  }

  private final SensorManager manager;
  private final EnumSet<Kind> selected;
  private final EnumMap<Kind, Sensor> active = new EnumMap<>(Kind.class);
  private final Consumer<Reading> consumer;
  private final Consumer<RuntimeException> failure;
  private final AtomicBoolean closed = new AtomicBoolean();
  private final long[] lastTimestampNs = new long[Kind.values().length];
  private HandlerThread thread;
  private boolean started;

  AndroidGuestSensorSource(
      Context context,
      EnumSet<Kind> selected,
      Consumer<Reading> consumer,
      Consumer<RuntimeException> failure) {
    manager = context.getSystemService(SensorManager.class);
    this.selected = selected.clone();
    this.consumer = consumer;
    this.failure = failure;
  }

  static EnumSet<Kind> available(Context context) {
    SensorManager manager = context.getSystemService(SensorManager.class);
    EnumSet<Kind> result = EnumSet.noneOf(Kind.class);
    if (manager != null)
      for (Kind kind : Kind.values())
        if (manager.getDefaultSensor(kind.androidType) != null) result.add(kind);
    return result;
  }

  synchronized int start() {
    if (started || closed.get()) throw new IllegalStateException("Sensor source already used");
    started = true;
    if (manager == null || selected.isEmpty()) return 0;
    thread = new HandlerThread("android-guest-sensors");
    thread.start();
    Handler handler = new Handler(thread.getLooper());
    try {
      for (Kind kind : selected) {
        Sensor sensor = manager.getDefaultSensor(kind.androidType);
        // Body/activity and vendor-private sensors are deliberately outside this whitelist.
        if (sensor != null && manager.registerListener(this, sensor, 20000, 0, handler))
          active.put(kind, sensor);
      }
      int mask = 0;
      for (Kind kind : active.keySet()) mask |= 1 << kind.guestId;
      return mask;
    } catch (RuntimeException error) {
      close();
      throw error;
    }
  }

  @Override
  public void onSensorChanged(SensorEvent event) {
    if (closed.get() || event.timestamp < 0) return;
    Kind kind = null;
    synchronized (this) {
      for (Map.Entry<Kind, Sensor> entry : active.entrySet())
        if (entry.getValue() == event.sensor) {
          kind = entry.getKey();
          break;
        }
    }
    if (kind == null || event.values.length < kind.axes) return;
    synchronized (this) {
      long previous = lastTimestampNs[kind.ordinal()];
      // SensorManager's requested period is a hint on some vendor HALs.
      // Bound actual forwarding even if callbacks arrive at a faster rate.
      if (event.timestamp <= previous || (previous != 0 && event.timestamp - previous < 20000000))
        return;
      lastTimestampNs[kind.ordinal()] = event.timestamp;
    }
    float[] values = new float[kind.axes];
    for (int i = 0; i < values.length; i++) {
      values[i] = event.values[i];
      if (!Float.isFinite(values[i])) return;
    }
    if (kind == Kind.PROXIMITY) values[0] = values[0] >= event.sensor.getMaximumRange() ? 1f : 0f;
    try {
      // close() waits for this bounded callback; queued events cannot leak into
      // a later guest session after this source has been revoked.
      synchronized (this) {
        if (!closed.get()) consumer.accept(new Reading(kind, event.timestamp, values));
      }
    } catch (RuntimeException error) {
      close();
      failure.accept(error);
    }
  }

  @Override
  public void onAccuracyChanged(Sensor sensor, int accuracy) {}

  @Override
  public void close() {
    if (!closed.compareAndSet(false, true)) return;
    synchronized (this) {
      if (manager != null) manager.unregisterListener(this);
      active.clear();
      if (thread != null) thread.quitSafely();
    }
  }
}
