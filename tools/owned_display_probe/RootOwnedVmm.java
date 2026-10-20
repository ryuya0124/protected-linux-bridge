package net.ryuya_dev.protectedlinux.displayroot;

import android.os.Looper;

/** Developer launcher for the separately built Android 17 VMM. No global display slot. */
public final class RootOwnedVmm {
  private static native int run(String[] arguments);

  public static void main(String[] arguments) {
    int result = 70;
    try {
      if (arguments.length < 10 || arguments.length > 160)
        throw new IllegalArgumentException("Fixed VMM arguments");
      Looper.prepareMainLooper();
      Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
      RootOwnedDisplayPublisher.initialize();
      System.load(
          "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-owned-runtime/libplb-vmm-entry.so");
      result = run(arguments);
    } catch (Throwable failure) {
      failure.printStackTrace(System.out);
    } finally {
      RootOwnedDisplayPublisher.close();
    }
    System.out.println("ANDROID17_OWNED_VMM_EXIT " + result);
    System.exit(result);
  }
}
