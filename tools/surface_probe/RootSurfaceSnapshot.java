package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.os.Looper;
import android.util.Base64;

/** One test assertion of the app's own GPU Surface, never a full host screenshot. */
public final class RootSurfaceSnapshot {
  public static void main(String[] args) {
    try {
      RootVmmLoadProbe.verifyHost();
      if (args.length != 0) System.exit(20);
      Looper.prepareMainLooper();
      Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
      byte[] image = RootSurfaceAccess.call("frame").getByteArray("png");
      if (image == null || image.length < 24 || image.length > 524288) System.exit(21);
      System.out.println("PLB_SURFACE_PNG " + Base64.encodeToString(image, Base64.NO_WRAP));
    } catch (Throwable error) {
      error.printStackTrace(System.out);
      System.exit(30);
    }
  }
}
