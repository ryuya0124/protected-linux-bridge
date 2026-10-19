package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.content.AttributionSource;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;

/** External provider reference for an explicitly started root utility. */
final class RootSurfaceAccess {
  static Bundle call(String method) throws Exception {
    Class<?> managerType = Class.forName("android.app.IActivityManager");
    Object manager =
        Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
    String authority = "net.ryuya_dev.protectedlinux.surfaceprobe";
    IBinder token = new Binder();
    Object holder =
        managerType
            .getMethod(
                "getContentProviderExternal", String.class, int.class, IBinder.class, String.class)
            .invoke(manager, authority, 0, token, "plb-surface-probe");
    if (holder == null) throw new IllegalStateException("Surface provider unavailable");
    try {
      Object provider = holder.getClass().getField("provider").get(holder);
      return (Bundle)
          Class.forName("android.content.IContentProvider")
              .getMethod(
                  "call",
                  AttributionSource.class,
                  String.class,
                  String.class,
                  String.class,
                  Bundle.class)
              .invoke(
                  provider,
                  new AttributionSource.Builder(0).build(),
                  authority,
                  method,
                  null,
                  null);
    } finally {
      managerType
          .getMethod("removeContentProviderExternal", String.class, IBinder.class)
          .invoke(manager, authority, token);
    }
  }
}
