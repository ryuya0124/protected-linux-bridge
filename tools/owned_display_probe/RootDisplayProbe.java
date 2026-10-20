package net.ryuya_dev.protectedlinux.displayroot;

import android.os.Looper;
import java.io.DataInputStream;

/** Developer-only GLES/owned-Surface qualification. Never creates a VM. */
public final class RootDisplayProbe {
  private static native int admit();

  private static native int start();

  private static native int frame(int color);

  private static native void stop();

  public static void main(String[] arguments) {
    int result = 30;
    try {
      boolean renderer = arguments.length == 1 && "renderer".equals(arguments[0]);
      if (arguments.length != 0 && !renderer) throw new IllegalArgumentException("Probe mode");
      Looper.prepareMainLooper();
      Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
      System.load(
          renderer
              ? "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-renderer-probe/libplb-renderer-probe.so"
              : "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-owned-runtime/libplb-display-probe.so");
      if (admit() != 0) throw new IllegalStateException("Probe admission");
      RootOwnedDisplayPublisher.initialize();
      int started = start();
      if (started != 0) throw new IllegalStateException("GLES setup: " + started);
      DataInputStream input = new DataInputStream(System.in);
      for (int index = 0; index < 3; index++) {
        int color = input.readInt();
        if ((color & 0xff000000) != 0 || frame(color) != 0)
          throw new IllegalStateException("Frame validation");
        System.out.printf("OWNED_PATTERN_PRESENTED %06x%n", color);
        System.out.flush();
      }
      if (input.readInt() != -1) throw new IllegalArgumentException("Probe end");
      result = 0;
    } catch (Throwable failure) {
      failure.printStackTrace(System.out);
    } finally {
      try {
        stop();
      } catch (Throwable ignored) {
      }
      RootOwnedDisplayPublisher.close();
    }
    System.out.println("OWNED_DISPLAY_PROBE_EXIT " + result);
    System.exit(result);
  }
}
