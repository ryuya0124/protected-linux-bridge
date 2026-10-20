package net.ryuya_dev.protectedlinux;

import android.content.Context;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.os.Handler;
import android.os.SystemClock;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** A visible Debian session's opt-in LED channel. Never opens a camera or changes host files. */
final class VmTorchBridge implements AutoCloseable {
  record Choice(String id, int maximum, int defaultLevel) {}

  static List<Choice> available(Context context) throws Exception {
    CameraManager manager = context.getSystemService(CameraManager.class);
    List<Choice> result = new ArrayList<>();
    if (manager == null) return result;
    for (String id : manager.getCameraIdList()) {
      var info = manager.getCameraCharacteristics(id);
      if (!Boolean.TRUE.equals(info.get(CameraCharacteristics.FLASH_INFO_AVAILABLE))) continue;
      Integer maximum = info.get(CameraCharacteristics.FLASH_INFO_STRENGTH_MAXIMUM_LEVEL);
      Integer normal = info.get(CameraCharacteristics.FLASH_INFO_STRENGTH_DEFAULT_LEVEL);
      int limit = maximum == null ? 1 : Math.min(255, maximum);
      result.add(new Choice(id, limit, normal == null ? 1 : Math.min(limit, normal)));
    }
    return List.copyOf(result);
  }

  private final Context context;
  private final Handler main;
  private final CameraManager manager;
  private final String cameraId;
  private final VmTorchLease lease;
  private final VmChannel channel;
  private final byte[] pending = new byte[VmTorchLease.REQUEST_BYTES];
  private int position;
  private int releaseAttempts;
  private volatile boolean closed;
  private final java.util.concurrent.atomic.AtomicBoolean inFlight =
      new java.util.concurrent.atomic.AtomicBoolean();
  private final Runnable expiry = this::expire;
  private final android.content.SharedPreferences.OnSharedPreferenceChangeListener preferences;
  private final CameraManager.TorchCallback callback =
      new CameraManager.TorchCallback() {
        @Override
        public void onTorchModeChanged(String id, boolean enabled) {
          if (!closed && cameraId.equals(id)) lease.observe(true, enabled);
        }

        @Override
        public void onTorchModeUnavailable(String id) {
          if (!closed && cameraId.equals(id)) lease.observe(false, false);
        }
      };

  private VmTorchBridge(Context context, Choice choice) throws Exception {
    this.context = context.getApplicationContext();
    main = new Handler(context.getMainLooper());
    manager = context.getSystemService(CameraManager.class);
    cameraId = choice.id();
    lease =
        new VmTorchLease(
            level -> {
              if (level == 0) manager.setTorchMode(cameraId, false);
              else if (choice.maximum() > 1) manager.turnOnTorchWithStrengthLevel(cameraId, level);
              else manager.setTorchMode(cameraId, true);
            },
            () -> !closed && enabled(this.context),
            SystemClock::elapsedRealtime,
            choice.maximum(),
            choice.defaultLevel());
    channel =
        new VmChannel(
            new VmChannel.Listener() {
              @Override
              public void bytes(byte[] data) {
                for (byte value : data) {
                  pending[position++] = value;
                  if (position == pending.length) {
                    byte[] request = Arrays.copyOf(pending, pending.length);
                    position = 0;
                    // The broker allows only one outstanding request, with a short
                    // reply deadline. A peer cannot enqueue an unlimited main-loop job.
                    if (!inFlight.compareAndSet(false, true)
                        || !main.post(
                            () -> {
                              try {
                                respond(request);
                              } finally {
                                inFlight.set(false);
                              }
                            })) throw new IllegalStateException("LED request backpressure");
                  }
                }
              }

              @Override
              public void ended(String error) {
                main.post(() -> close());
              }
            });
    preferences =
        (prefs, key) -> {
          if (key != null && (key.equals("enabled") || key.startsWith("vm_torch_"))) close();
        };
    try {
      manager.registerTorchCallback(callback, main);
      this.context
          .getSharedPreferences("config", 0)
          .registerOnSharedPreferenceChangeListener(preferences);
      channel.open('T'); // Manager refuses this unless a VM already exists.
    } catch (Exception failed) {
      close();
      throw failed;
    }
  }

  private static boolean enabled(Context context) {
    var preferences = context.getSharedPreferences("config", 0);
    return preferences.getBoolean("enabled", true)
        && preferences.getBoolean("vm_torch_enabled", false);
  }

  static VmTorchBridge open(Context context) {
    if (!enabled(context)) return null;
    try {
      String selected = context.getSharedPreferences("config", 0).getString("vm_torch_camera", "");
      Choice choice =
          available(context).stream()
              .filter(c -> c.id().equals(selected))
              .findFirst()
              .orElseThrow();
      return new VmTorchBridge(context, choice);
    } catch (Exception unavailable) {
      android.util.Log.w(
          "ProtectedLinux", "VM_TORCH_UNAVAILABLE " + unavailable.getClass().getSimpleName());
      return null;
    }
  }

  private void respond(byte[] request) {
    if (closed) return;
    channel.write(lease.request(request));
    main.removeCallbacks(expiry);
    long remaining = lease.remainingMillis();
    if (remaining > 0) main.postDelayed(expiry, remaining);
  }

  private void expire() {
    if (!closed && !lease.expire()) {
      android.util.Log.e("ProtectedLinux", "VM_TORCH_OFF_FAILED");
      close();
    }
  }

  @Override
  public void close() {
    if (android.os.Looper.myLooper() != main.getLooper()) {
      main.post(this::close);
      return;
    }
    if (closed) return;
    closed = true;
    main.removeCallbacks(expiry);
    channel.close();
    context
        .getSharedPreferences("config", 0)
        .unregisterOnSharedPreferenceChangeListener(preferences);
    releaseLamp();
    manager.unregisterTorchCallback(callback);
  }

  private void releaseLamp() {
    try {
      lease.close();
    } catch (RuntimeException failed) {
      if (++releaseAttempts < 5) main.postDelayed(this::releaseLamp, 250);
      else android.util.Log.e("ProtectedLinux", "VM_TORCH_OFF_FAILED retries_exhausted=1");
    }
  }
}
