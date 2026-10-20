package net.ryuya_dev.protectedlinux;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/** No host Activity, Surface, screenshots or input. Guest graphics stay offscreen. */
public final class HeadlessGuestInstrumentation extends Instrumentation {
  private static final String BASE =
      "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-owned-runtime";

  @Override
  public void onCreate(Bundle arguments) {
    super.onCreate(arguments);
    start();
  }

  private String fixedHelper(String name) throws Exception {
    if (!name.equals("guest-inspect") && !name.equals("shutdown-guest"))
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
    OwnedDisplayConnection connection = null;
    LinkedBlockingQueue<String> lines = new LinkedBlockingQueue<>();
    StringBuilder log = new StringBuilder();
    boolean booted = false, shutdownRequested = false;
    int status = Activity.RESULT_CANCELED;
    try {
      connection = new OwnedDisplayConnection();
      producer =
          new ProcessBuilder("su", "-M", "-c", BASE + "/vm-controller --run-approved-gles-reclaim")
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
      long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(145);
      while (System.nanoTime() < deadline) {
        String line = lines.poll(100, TimeUnit.MILLISECONDS);
        if (line == null) continue;
        if (line.contains("PLB_ANDROID_MEMORY_HOME")) {
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
      if (!inventory.contains("PLB_GUEST_PERIPHERAL_INSPECT_COMPLETE"))
        throw new IOException("Guest metadata completion missing");
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
      if (connection != null) connection.close();
      synchronized (log) {
        result.putString("producer_log", log.toString());
      }
      finish(status, result);
    }
  }
}
