package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.os.Bundle;
import android.os.Looper;
import android.os.Process;
import android.view.Surface;

/** app_process root entry: Binder-parcel a Surface, then render in this process. */
public final class RootSurfaceProbe {
  private static native int draw(Surface surface);

  public static void main(String[] args) {
    try {
      if (Process.myUid() != 0
          || args.length != 1
          || !args[0].equals(
              "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/libsurface-probe.so"))
        System.exit(20);
      Looper.prepareMainLooper();
      Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
      Bundle result = RootSurfaceAccess.call("surface");
      Surface surface = result == null ? null : result.getParcelable("surface", Surface.class);
      if (surface == null || !surface.isValid()) System.exit(21);
      System.load(args[0]);
      int code = draw(surface);
      if (code == 0) {
        Bundle sample = RootSurfaceAccess.call("sample");
        System.out.printf(
            "PLB_SURFACE_PIXEL status=%d argb=%08x%n",
            sample.getInt("sample_status"), sample.getInt("sample_color"));
        if (sample.getInt("sample_status") != 0 || sample.getInt("sample_color") != 0xff0000ff)
          code = 24;
      }
      surface.release();
      System.exit(code);
    } catch (Throwable error) {
      error.printStackTrace(System.out);
      System.exit(30);
    }
  }
}
