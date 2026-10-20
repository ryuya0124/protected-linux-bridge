package net.ryuya_dev.protectedlinux;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.EnumSet;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/** No host Activity, Surface, screenshots or input. Guest graphics stay offscreen. */
public final class HeadlessGuestInstrumentation extends Instrumentation {
  private static final String BASE =
      "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-owned-runtime";

  private boolean appRuntime;
  private boolean relaySensors;
  private boolean nullAudio;
  private boolean syntheticCamera;

  @Override
  public void onCreate(Bundle arguments) {
    super.onCreate(arguments);
    appRuntime = arguments != null && "true".equals(arguments.getString("app_runtime"));
    relaySensors = arguments != null && "true".equals(arguments.getString("relay_sensors"));
    nullAudio = arguments != null && "true".equals(arguments.getString("null_audio"));
    syntheticCamera = arguments != null && "true".equals(arguments.getString("synthetic_camera"));
    start();
  }

  private String fixedHelper(String name) throws Exception {
    if (!name.equals("guest-inspect")
        && !name.equals("guest-sensor-check")
        && !name.equals("guest-audio-check")
        && !name.equals("guest-camera-check")
        && !name.equals("shutdown-guest"))
      throw new IllegalArgumentException("Fixed guest helper only");
    Process helper =
        new ProcessBuilder("su", "-M", "-c", BASE + "/" + name).redirectErrorStream(true).start();
    var reader = Executors.newSingleThreadExecutor();
    try {
      var bytes = reader.submit(() -> helper.getInputStream().readNBytes(2097153));
      if (!helper.waitFor(20, TimeUnit.SECONDS)) {
        helper.destroy();
        throw new IOException("Guest metadata helper deadline");
      }
      byte[] output = bytes.get(1, TimeUnit.SECONDS);
      if (output.length > 2097152) throw new IOException("Guest metadata output bound");
      return new String(output, java.nio.charset.StandardCharsets.UTF_8);
    } finally {
      helper.getInputStream().close();
      reader.shutdownNow();
    }
  }

  @Override
  public void onStart() {
    Bundle result = new Bundle();
    Process producer = null;
    android.net.LocalSocket appControl = null;
    OwnedDisplayConnection connection = null;
    AndroidGuestSensorSource sensors = null;
    AndroidGuestSensorPipe sensorPipe = null;
    LinkedBlockingQueue<String> lines = new LinkedBlockingQueue<>();
    StringBuilder log = new StringBuilder();
    boolean booted = false, shutdownRequested = false;
    int status = Activity.RESULT_CANCELED;
    try {
      connection = new OwnedDisplayConnection();
      int sensorMask = 0;
      if (relaySensors) {
        sensorPipe = new AndroidGuestSensorPipe();
        AndroidGuestSensorPipe stream = sensorPipe;
        sensors =
            new AndroidGuestSensorSource(
                getTargetContext(),
                EnumSet.of(AndroidGuestSensorSource.Kind.ACCELEROMETER),
                stream::offer,
                failure -> lines.offer("HEADLESS_SENSOR_SOURCE_FAILED"));
        sensorMask = sensors.start();
        if (sensorMask != 1) throw new IOException("Selected accelerometer unavailable");
      }
      producer =
          appRuntime
              ? AndroidGuestRuntime.start(getTargetContext(), 1024, true, true)
              : new ProcessBuilder(
                      "su",
                      "-M",
                      "-c",
                      BASE
                          + (nullAudio ? "/vm-controller-audio " : "/vm-controller ")
                          + (relaySensors
                              ? "--run-approved-gles-sensors"
                              : "--run-approved-gles-reclaim"))
                  .redirectErrorStream(true)
                  .start();
      Process owned = producer;
      Thread reader =
          new Thread(
              () -> {
                try (BufferedReader input =
                    new BufferedReader(new InputStreamReader(owned.getInputStream()))) {
                  String line;
                  while ((line = input.readLine()) != null) {
                    synchronized (log) {
                      log.append(line).append('\n');
                    }
                    lines.offer(line);
                  }
                } catch (IOException error) {
                  lines.offer("HEADLESS_READER_FAILED");
                } finally {
                  lines.offer("HEADLESS_PRODUCER_EOF");
                }
              },
              "headless-owned-vmm");
      reader.setDaemon(true);
      reader.start();
      connection.writeCapability(producer.getOutputStream());
      if (relaySensors) sensorPipe.start(producer.getOutputStream(), sensorMask);
      if (appRuntime) {
        long connectDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(90);
        while (System.nanoTime() < connectDeadline && producer.isAlive()) {
          var candidate = new android.net.LocalSocket();
          try {
            candidate.connect(
                new android.net.LocalSocketAddress(
                    "net.ryuya_dev.protectedlinux.android-display",
                    android.net.LocalSocketAddress.Namespace.ABSTRACT));
            if (candidate.getPeerCredentials().getUid() != 0)
              throw new IOException("App control peer");
            candidate.setSoTimeout(120000);
            byte[] header = candidate.getInputStream().readNBytes(20);
            var words = java.nio.ByteBuffer.wrap(header).order(java.nio.ByteOrder.LITTLE_ENDIAN);
            if (header.length != 20
                || words.getInt() != 0x41424c50
                || words.getInt() != 1
                || words.getInt() != 480
                || words.getInt() != 800
                || words.getInt() != 0) throw new IOException("App control greeting");
            appControl = candidate;
            break;
          } catch (IOException unavailable) {
            candidate.close();
            Thread.sleep(100);
          }
        }
        if (appControl == null) throw new IOException("App control unavailable");
      }

      long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(145);
      while (System.nanoTime() < deadline) {
        String line = lines.poll(100, TimeUnit.MILLISECONDS);
        if (line == null) continue;
        if (line.equals("HEADLESS_SENSOR_SOURCE_FAILED"))
          throw new IOException("Host sensor source stopped");
        if (line.contains(appRuntime ? "ANDROID17_APP_GUEST_READY" : "PLB_ANDROID_MEMORY_HOME")) {
          booted = true;
          break;
        }
        if (line.contains("ANDROID17_DEVELOPER_SESSION_EXIT")
            || line.equals("HEADLESS_PRODUCER_EOF"))
          throw new IOException("Headless guest stopped before boot: " + line);
      }
      if (!booted) throw new IOException("Headless Android boot deadline");
      String inventory = fixedHelper("guest-inspect");
      result.putString("guest_inventory", inventory);
      if (!inventory.contains("PLB_GUEST_PERIPHERAL_INSPECT_COMPLETE")
          || (appRuntime && !inventory.contains("BOOT=1\n")))
        throw new IOException("Guest metadata completion missing");
      if (relaySensors) {
        String sensorCheck = fixedHelper("guest-sensor-check");
        result.putString("sensor_check", sensorCheck);
        result.putLong("sensor_pipe_written", sensorPipe.written());
        if (!sensorCheck.contains("PLB_GUEST_SENSOR_EVENTS_PASS") || sensorPipe.failure() != null)
          throw new IOException("Guest sensor event verification failed");
      }
      if (nullAudio) {
        String audioCheck = fixedHelper("guest-audio-check");
        result.putString("audio_check", audioCheck);
        if (!audioCheck.contains("PLB_GUEST_AUDIO_HAL_PASS"))
          throw new IOException("Guest audio HAL verification failed");
      }
      if (syntheticCamera) {
        String cameraCheck = fixedHelper("guest-camera-check");
        result.putString("camera_check", cameraCheck);
        if (!cameraCheck.contains("PLB_GUEST_CAMERA_HAL_PASS")
            || !cameraCheck.contains("PLB_GUEST_CAMERA_REVOKED=0"))
          throw new IOException("Guest synthetic camera HAL verification failed");
      }
      result.putString("guest_shutdown", fixedHelper("shutdown-guest"));
      shutdownRequested = true;
      if (!producer.waitFor(20, TimeUnit.SECONDS)) throw new IOException("Owned shutdown deadline");
      reader.join(1000);
      synchronized (log) {
        if (log.indexOf("ANDROID17_OWNED_VMM_EXIT 0") < 0
            || log.indexOf("ANDROID17_DEVELOPER_SESSION_EXIT 0") < 0)
          throw new IOException("Metadata collected, but owned session exit was not clean");
      }
      status = Activity.RESULT_OK;
      result.putString("result", "HEADLESS_ANDROID_GUEST_METADATA_COMPLETE");
    } catch (Exception failure) {
      result.putString("error", failure.getClass().getSimpleName() + ": " + failure.getMessage());
    } finally {
      if (sensors != null) sensors.close();
      if (sensorPipe != null) sensorPipe.close();
      if (booted && !shutdownRequested && producer != null && producer.isAlive()) {
        try {
          result.putString("guest_shutdown_recovery", fixedHelper("shutdown-guest"));
          producer.waitFor(20, TimeUnit.SECONDS);
        } catch (Exception failure) {
          result.putString("cleanup_error", failure.getClass().getSimpleName());
        }
      }
      if (producer != null) {
        try {
          producer.getOutputStream().close();
        } catch (IOException ignored) {
        }
        if (producer.isAlive()) producer.destroy();
      }
      if (appControl != null)
        try {
          appControl.close();
        } catch (IOException ignored) {
        }
      if (connection != null) connection.close();
      synchronized (log) {
        result.putString("producer_log", log.toString());
      }
      finish(status, result);
    }
  }
}
