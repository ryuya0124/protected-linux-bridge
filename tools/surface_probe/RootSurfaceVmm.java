package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.os.Bundle;
import android.os.Looper;
import android.system.Os;
import android.view.Surface;
import java.io.FileDescriptor;

/** Internal entry for the separate guarded developer VM; not an exported Android component. */
public final class RootSurfaceVmm {
  private static native int run(FileDescriptor library, Surface surface, String[] arguments);

  public static void main(String[] args) {
    try {
      RootVmmLoadProbe.verifyHost();
      if (args.length < 2
          || args.length > 160
          || !args[0].equals(
              "/data/user/0/net.ryuya_dev.protectedlinux/files/android-gpu4g-surface-runtime/crosvm"))
        System.exit(20);
      Looper.prepareMainLooper();
      Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
      Bundle result = RootSurfaceAccess.call("surface");
      Surface surface = result == null ? null : result.getParcelable("surface", Surface.class);
      if (surface == null || !surface.isValid())
        throw new IllegalStateException("Surface unavailable");
      FileDescriptor library = RootVmmLoadProbe.openLibrary();
      int code;
      try {
        System.load(RootVmmLoadProbe.BASE + "libsurface-probe.so");
        code = run(library, surface, args);
      } finally {
        Os.close(library);
        surface.release();
      }
      System.out.println("PLB_SURFACE_VMM_EXIT " + code);
      System.exit(code);
    } catch (Throwable error) {
      error.printStackTrace(System.out);
      System.exit(30);
    }
  }
}
