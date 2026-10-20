package net.ryuya_dev.protectedlinux;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.FrameLayout;
import java.io.*;
import java.security.SecureRandom;
import java.util.concurrent.*;

/** Same-signer developer instrumentation; absent from the distributed application. */
public final class DisplayProbeInstrumentation extends Instrumentation {
  private boolean vm;
  private boolean renderer;
  private boolean reclaim;
  private float requestedHz;
  private volatile boolean rateRefused;
  private int holdSeconds;
  private Activity activity;
  private FrameLayout container;
  private SurfaceView view;
  private OwnedDisplayConnection connection;
  private java.lang.Process producer;
  private final BlockingQueue<String> messages = new LinkedBlockingQueue<>();
  private final StringBuilder output = new StringBuilder();
  private volatile long generation;

  @Override
  public void onCreate(Bundle arguments) {
    super.onCreate(arguments);
    vm = arguments != null && "true".equals(arguments.getString("vm"));
    renderer = arguments != null && "true".equals(arguments.getString("renderer"));
    reclaim = arguments != null && "true".equals(arguments.getString("reclaim"));
    String rate = arguments == null ? null : arguments.getString("rate_hz");
    if (rate != null) {
      requestedHz = Float.parseFloat(rate);
      if (requestedHz != 60f && requestedHz != 120f)
        throw new IllegalArgumentException("Reviewed display rates: 60 or 120 Hz");
    }
    String hold = arguments == null ? null : arguments.getString("hold_seconds");
    if (hold != null) {
      holdSeconds = Integer.parseInt(hold);
      if (holdSeconds != 0 && holdSeconds != 30 && holdSeconds != 60)
        throw new IllegalArgumentException("Reviewed hold times: 0, 30 or 60 seconds");
    }
    start();
  }

  @Override
  public void onStart() {
    Bundle result = new Bundle();
    SharedPreferences preferences = getTargetContext().getSharedPreferences("config", 0);
    boolean hadEnabled = preferences.contains("enabled");
    boolean enabled = preferences.getBoolean("enabled", true);
    int status = Activity.RESULT_CANCELED;
    try {
      if (!preferences.edit().putBoolean("enabled", false).commit())
        throw new IOException("Disable implicit manager preparation");
      connection = new OwnedDisplayConnection();
      Intent intent =
          new Intent()
              .setClassName(getTargetContext(), "net.ryuya_dev.protectedlinux.MainActivity")
              .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
      activity = startActivitySync(intent);
      runOnMainSync(
          () -> {
            activity
                .getWindow()
                .addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            container = new FrameLayout(activity);
            container.setBackgroundColor(0xff101010);
            activity.setContentView(container);
            replaceSurface();
          });
      waitForIdleSync();
      waitForSurface();
      String base = "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-owned-runtime";
      String command =
          "LD_LIBRARY_PATH="
              + base
              + ":/apex/com.android.virt/lib64:/system/lib64 CLASSPATH="
              + base
              + "/display-probe.jar timeout 35 /system/bin/app_process /system/bin "
              + "net.ryuya_dev.protectedlinux.displayroot.RootDisplayProbe";
      if (renderer) {
        if (vm) throw new IOException("Choose one probe mode");
        command =
            command.replace(
                    "LD_LIBRARY_PATH=",
                    "LD_LIBRARY_PATH="
                        + "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-renderer-probe:")
                + " renderer";
      }
      if (vm) command = base + "/vm-controller --run-approved-gles" + (reclaim ? "-reclaim" : "");
      producer = new ProcessBuilder("su", "-M", "-c", command).redirectErrorStream(true).start();
      Thread reader =
          new Thread(
              () -> {
                try (BufferedReader lines =
                    new BufferedReader(new InputStreamReader(producer.getInputStream()))) {
                  String line;
                  while ((line = lines.readLine()) != null) {
                    synchronized (output) {
                      output.append(line).append('\n');
                    }
                    messages.add(line);
                  }
                } catch (IOException failure) {
                  messages.add("READER_FAILED");
                } finally {
                  messages.add("PRODUCER_EOF");
                }
              },
              "owned-probe-output");
      reader.setDaemon(true);
      reader.start();
      connection.writeCapability(producer.getOutputStream());
      if (vm) {
        qualifyVm(base);
        status = Activity.RESULT_OK;
        result.putString("result", "ANDROID17_BOOT_GLES_SURFACE_RECONNECT_PASS");
        return;
      }
      DataOutputStream controls = new DataOutputStream(producer.getOutputStream());
      SecureRandom random = new SecureRandom();
      int color = 0;
      for (int phase = 0; phase < 3; phase++) {
        if (phase == 2) {
          runOnMainSync(
              () -> {
                connection.detach(generation, false);
                container.removeView(view);
                replaceSurface();
              });
          waitForIdleSync();
          waitForSurface();
          assertScreenColor(color, "reattached");
        }
        color =
            ((32 + random.nextInt(192)) << 16)
                | ((32 + random.nextInt(192)) << 8)
                | (32 + random.nextInt(192));
        controls.writeInt(color);
        controls.flush();
        awaitLine(String.format("OWNED_PATTERN_PRESENTED %06x", color));
        assertScreenColor(color, "phase-" + phase);
      }
      controls.writeInt(-1);
      controls.flush();
      awaitLine("OWNED_DISPLAY_PROBE_EXIT 0");
      if (!producer.waitFor(5, TimeUnit.SECONDS)) throw new IOException("Producer did not exit");
      status = Activity.RESULT_OK;
      result.putString("result", "OWNED_GLES_SURFACE_RECONNECT_PASS");
    } catch (Throwable failure) {
      StringWriter stack = new StringWriter();
      failure.printStackTrace(new PrintWriter(stack));
      result.putString("error", stack.toString());
    } finally {
      if (producer != null) {
        try {
          producer.getOutputStream().close();
        } catch (IOException ignored) {
        }
        try {
          if (!producer.waitFor(3, TimeUnit.SECONDS)) producer.destroy();
        } catch (InterruptedException ignored) {
          Thread.currentThread().interrupt();
        }
      }
      if (connection != null) connection.close();
      if (activity != null) runOnMainSync(() -> activity.finish());
      SharedPreferences.Editor editor = preferences.edit();
      if (hadEnabled) editor.putBoolean("enabled", enabled);
      else editor.remove("enabled");
      if (!editor.commit()) result.putString("restore_error", "Config restore failed");
      synchronized (output) {
        result.putString("producer_log", output.toString());
      }
      finish(status, result);
    }
  }

  private void qualifyVm(String base) throws Exception {
    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(160);
    boolean home = false, adreno = false;
    while (System.nanoTime() < deadline) {
      String line = messages.poll(100, TimeUnit.MILLISECONDS);
      if (line == null) continue;
      if (line.equals("PRODUCER_EOF") || line.contains("ANDROID17_INPUT_REFUSED"))
        throw new IOException("VM launcher stopped: " + line);
      adreno |= line.contains("GLES:") && line.contains("Adreno");
      if (line.contains("PLB_ANDROID_MEMORY_HOME")) {
        home = true;
        break;
      }
      if (line.contains("ANDROID17_DEVELOPER_SESSION_EXIT"))
        throw new IOException("VM stopped before Android home: " + line);
    }
    if (!home || !adreno) throw new IOException("Android home/Adreno not verified");
    Thread.sleep(1500);
    saveVmFrame("vm-live");
    runOnMainSync(
        () -> {
          connection.detach(generation, false);
          container.removeView(view);
          replaceSurface();
        });
    waitForIdleSync();
    waitForSurface();
    Thread.sleep(1500);
    saveVmFrame("vm-reattached");
    if (holdSeconds > 0) {
      Bundle holding = new Bundle();
      holding.putString("phase", "VM_HOLD_READY");
      sendStatus(1, holding);
      long holdDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(holdSeconds);
      while (System.nanoTime() < holdDeadline) {
        if (!producer.isAlive()) throw new IOException("VM stopped during observation");
        Thread.sleep(100);
      }
      saveVmFrame("vm-after-hold");
    }
    java.lang.Process shutdown =
        new ProcessBuilder("su", "-M", "-c", base + "/shutdown-guest")
            .redirectErrorStream(true)
            .start();
    if (!shutdown.waitFor(15, TimeUnit.SECONDS)) {
      shutdown.destroy();
      throw new IOException("Guest shutdown timeout");
    }
    awaitLine("ANDROID17_DEVELOPER_SESSION_EXIT 0");
    if (!producer.waitFor(5, TimeUnit.SECONDS)) throw new IOException("Controller exit timeout");
  }

  private void saveVmFrame(String phase) throws Exception {
    int[] location = new int[2];
    runOnMainSync(() -> view.getLocationOnScreen(location));
    Bitmap capture = getUiAutomation().takeScreenshot();
    if (capture == null) throw new IOException("Host screenshot unavailable");
    Bitmap area = Bitmap.createBitmap(capture, location[0], location[1], 480, 800);
    java.util.Set<Integer> samples = new java.util.HashSet<>();
    for (int y = 20; y < 780; y += 31)
      for (int x = 20; x < 460; x += 29) samples.add(area.getPixel(x, y));
    File directory = new File(getTargetContext().getCacheDir(), "owned-display-probe");
    if (!directory.isDirectory() && !directory.mkdir()) throw new IOException("Evidence directory");
    try (OutputStream file = new FileOutputStream(new File(directory, phase + ".png"))) {
      area.compress(Bitmap.CompressFormat.PNG, 100, file);
    }
    area.recycle();
    capture.recycle();
    if (samples.size() < 8)
      throw new IOException("VM Surface is blank/unverified: " + samples.size());
  }

  private void replaceSurface() {
    generation = 0;
    view = new SurfaceView(activity);
    long[] attachedGeneration = new long[1];
    FrameLayout.LayoutParams layout = new FrameLayout.LayoutParams(480, 800);
    layout.leftMargin = 100;
    layout.topMargin = 150;
    view.getHolder()
        .addCallback(
            new SurfaceHolder.Callback() {
              @Override
              public void surfaceCreated(SurfaceHolder holder) {
                requestSurfaceRate(holder);
                attachedGeneration[0] = connection.attach(holder.getSurface(), false);
                generation = attachedGeneration[0];
              }

              @Override
              public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                attachedGeneration[0] = connection.attach(holder.getSurface(), false);
                generation = attachedGeneration[0];
              }

              @Override
              public void surfaceDestroyed(SurfaceHolder holder) {
                connection.detach(attachedGeneration[0], false);
              }
            });
    container.addView(view, layout);
  }

  private void waitForSurface() throws Exception {
    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
    while (System.nanoTime() < deadline) {
      if (rateRefused) throw new IOException("Requested owned-Surface rate unavailable");
      if (view.getHolder().getSurface().isValid() && generation > 0) return;
      Thread.sleep(30);
    }
    throw new IOException(
        "Surface did not become ready: valid="
            + view.getHolder().getSurface().isValid()
            + " generation="
            + generation
            + " attached="
            + view.isAttachedToWindow()
            + " size="
            + view.getWidth()
            + "x"
            + view.getHeight()
            + " finishing="
            + activity.isFinishing()
            + " destroyed="
            + activity.isDestroyed()
            + " focus="
            + view.hasWindowFocus());
  }

  private void requestSurfaceRate(SurfaceHolder holder) {
    if (requestedHz == 0) return;
    android.view.Display display = activity.getDisplay();
    android.view.Display.Mode current = display == null ? null : display.getMode();
    boolean supported = false;
    if (current != null) {
      for (android.view.Display.Mode mode : display.getSupportedModes()) {
        supported |=
            mode.getPhysicalWidth() == current.getPhysicalWidth()
                && mode.getPhysicalHeight() == current.getPhysicalHeight()
                && Math.abs(mode.getRefreshRate() - requestedHz) < 0.5f;
      }
    }
    if (!supported) {
      rateRefused = true;
      return;
    }
    try {
      holder
          .getSurface()
          .setFrameRate(
              requestedHz,
              android.view.Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
              android.view.Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
      synchronized (output) {
        // A successful request is not a measurement of presented frame rate.
        output
            .append("OWNED_RATE_REQUEST hz=")
            .append(requestedHz)
            .append(" display_hz_at_request=")
            .append(display.getRefreshRate())
            .append('\n');
      }
    } catch (RuntimeException failure) {
      rateRefused = true;
    }
  }

  private void awaitLine(String expected) throws Exception {
    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(8);
    while (System.nanoTime() < deadline) {
      String line = messages.poll(100, TimeUnit.MILLISECONDS);
      if (expected.equals(line)) return;
      if (line != null && line.startsWith("OWNED_DISPLAY_PROBE_EXIT")) break;
    }
    throw new IOException("Missing marker: " + expected);
  }

  private void assertScreenColor(int color, String phase) throws Exception {
    int[] location = new int[2];
    runOnMainSync(() -> view.getLocationOnScreen(location));
    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
    while (System.nanoTime() < deadline) {
      Bitmap capture = getUiAutomation().takeScreenshot();
      if (capture != null) {
        int actual = capture.getPixel(location[0] + 240, location[1] + 400);
        boolean match = true;
        for (int shift : new int[] {0, 8, 16})
          match &= Math.abs(((actual >> shift) & 255) - ((color >> shift) & 255)) <= 4;
        if (match) {
          File directory = new File(getTargetContext().getCacheDir(), "owned-display-probe");
          if (!directory.isDirectory() && !directory.mkdir())
            throw new IOException("Evidence directory");
          try (OutputStream file = new FileOutputStream(new File(directory, phase + ".png"))) {
            capture.compress(Bitmap.CompressFormat.PNG, 100, file);
          }
          capture.recycle();
          return;
        }
        capture.recycle();
      }
      Thread.sleep(60);
    }
    throw new IOException("Presented pixel mismatch: " + phase);
  }
}
