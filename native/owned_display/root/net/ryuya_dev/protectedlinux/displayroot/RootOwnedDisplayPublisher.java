package net.ryuya_dev.protectedlinux.displayroot;

import android.content.AttributionSource;
import android.os.*;
import android.system.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.concurrent.*;
import net.ryuya_dev.protectedlinux.display.DisplayCapability;

/** Used inside the owned root app_process before its private VMM starts. No main/VM entry. */
public final class RootOwnedDisplayPublisher {
  private static final String AUTHORITY = "net.ryuya_dev.protectedlinux.gpu-surface";
  private static final String DIRECTORY =
      "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-owned-runtime";
  private static volatile DisplayCapability capability;

  private static native int configure(int viewerUid, String session);

  private RootOwnedDisplayPublisher() {}

  public static synchronized void initialize() throws Exception {
    if (capability != null
        || android.os.Process.myUid() != 0
        || Build.VERSION.SDK_INT != 37
        || !Build.MODEL.equals(OwnedDisplayBuild.MODEL)
        || !Build.VERSION.INCREMENTAL.equals(OwnedDisplayBuild.BUILD)
        || !Os.uname().release.equals(OwnedDisplayBuild.KERNEL))
      throw new SecurityException("Owned display host");
    ExecutorService reader =
        Executors.newSingleThreadExecutor(
            r -> {
              Thread thread = new Thread(r, "plb-display-capability");
              thread.setDaemon(true);
              return thread;
            });
    try {
      capability = reader.submit(() -> DisplayCapability.read(System.in)).get(5, TimeUnit.SECONDS);
    } finally {
      reader.shutdownNow();
    }
    try {
      Bundle claim = call("display.claim", null);
      if (claim == null
          || claim.getInt("protocol") != 1
          || claim.getInt("viewer_uid") != capability.viewerUid)
        throw new SecurityException("Owned display lease");
      verifyBackend();
      System.load(DIRECTORY + "/libplb_owned_display_backend.so");
      if (configure(capability.viewerUid, capability.session) != 0)
        throw new SecurityException("Owned display configuration");
    } catch (Exception | LinkageError failure) {
      close();
      throw failure;
    }
  }

  private static void verifyBackend() throws Exception {
    var directory = Os.lstat(DIRECTORY);
    if (!OsConstants.S_ISDIR(directory.st_mode)
        || directory.st_uid != 0
        || (directory.st_mode & 0777) != 0700)
      throw new SecurityException("Private display runtime");
    FileDescriptor fd =
        Os.open(
            DIRECTORY + "/libplb_owned_display_backend.so",
            OsConstants.O_RDONLY | OsConstants.O_NOFOLLOW | OsConstants.O_CLOEXEC,
            0);
    try (FileInputStream input = new FileInputStream(fd)) {
      var before = Os.fstat(fd);
      if (!OsConstants.S_ISREG(before.st_mode)
          || before.st_uid != 0
          || before.st_nlink != 1
          || (before.st_mode & 0777) != 0500
          || before.st_size > 8 * 1024 * 1024)
        throw new SecurityException("Private display artifact");
      MessageDigest hash = MessageDigest.getInstance("SHA-256");
      byte[] buffer = new byte[65536];
      int count;
      while ((count = input.read(buffer)) != -1) hash.update(buffer, 0, count);
      if (!HexFormat.of().formatHex(hash.digest()).equals(OwnedDisplayBuild.BACKEND_SHA256))
        throw new SecurityException("Private display digest");
      var after = Os.lstat(DIRECTORY + "/libplb_owned_display_backend.so");
      if (after.st_dev != before.st_dev || after.st_ino != before.st_ino)
        throw new SecurityException("Private display artifact changed");
    }
  }

  // Called only from this process's native backend, including native worker threads.
  private static boolean exchange(IBinder display, boolean published) {
    try {
      Bundle result = call(published ? "display.publish" : "display.clear", display);
      return result != null && result.getBoolean("accepted");
    } catch (Exception error) {
      android.util.Log.w("ProtectedLinuxAndroid", "OWNED_DISPLAY_PUBLICATION_FAILED");
      return false;
    }
  }

  public static synchronized void close() {
    if (capability == null) return;
    try {
      call("display.clear", null);
    } catch (Exception ignored) {
    }
    // The process is single-use: capability is deliberately not reset for retry.
  }

  private static Bundle call(String method, IBinder display) throws Exception {
    if (capability == null) throw new SecurityException("Display capability unavailable");
    Bundle extras = new Bundle();
    extras.putString("session", capability.session);
    if (display != null) extras.putBinder("display", display);
    Class<?> managerType = Class.forName("android.app.IActivityManager");
    Object manager =
        Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
    IBinder token = new Binder();
    Object holder =
        managerType
            .getMethod(
                "getContentProviderExternal", String.class, int.class, IBinder.class, String.class)
            .invoke(manager, AUTHORITY, 0, token, "plb-owned-display");
    if (holder == null) throw new IllegalStateException("Owned display provider unavailable");
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
                  AUTHORITY,
                  method,
                  null,
                  extras);
    } finally {
      managerType
          .getMethod("removeContentProviderExternal", String.class, IBinder.class)
          .invoke(manager, AUTHORITY, token);
    }
  }
}
