package net.ryuya_dev.protectedlinux;

import android.content.Context;
import android.net.Uri;
import java.io.*;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.concurrent.TimeUnit;

/** Fixed verified guest image, separate persistent profile, stock remains available. */
final class KernelProfiles {
  static final String SHA = "95fe64403ffd031ce1ba8965098eed3073ae6db38be017801d1d77117f966dbc";

  private static boolean command(Context c, String args, int seconds) {
    try {
      String binary = new File(c.getFilesDir(), "runtime/debian-mainline-bridge").getAbsolutePath();
      Process p =
          new ProcessBuilder("su", "-M", "-c", binary + " " + args)
              .redirectErrorStream(true)
              .start();
      boolean done = p.waitFor(seconds, TimeUnit.SECONDS);
      if (!done) {
        p.destroyForcibly();
        return false;
      }
      return p.exitValue() == 0;
    } catch (Exception error) {
      return false;
    }
  }

  static boolean install(Context c, Uri uri) {
    File stage = new File(c.getFilesDir(), "kernel-import.pending");
    try {
      MessageDigest hash = MessageDigest.getInstance("SHA-256");
      long size = 0;
      try (InputStream in = c.getContentResolver().openInputStream(uri);
          OutputStream out = new FileOutputStream(stage)) {
        if (in == null) return false;
        byte[] buffer = new byte[65536];
        int n;
        while ((n = in.read(buffer)) != -1) {
          size += n;
          if (size > 17514504) return false;
          hash.update(buffer, 0, n);
          out.write(buffer, 0, n);
        }
      }
      if (size != 17514504 || !HexFormat.of().formatHex(hash.digest()).equals(SHA)) return false;
      return command(c, "--install-kernel " + android.os.Process.myUid(), 15);
    } catch (Exception error) {
      android.util.Log.w(
          "ProtectedLinux", "KERNEL_IMPORT_FAILED " + error.getClass().getSimpleName());
      return false;
    } finally {
      if (stage.exists() && !stage.delete())
        android.util.Log.w("ProtectedLinux", "KERNEL_IMPORT_CLEANUP_FAILED");
    }
  }

  static boolean select(Context c, boolean mainline) {
    if (mainline && !command(c, "--kernel-ready", 10)) return false;
    if (!RuntimeSetup.stop(c)) return false;
    if (mainline && !command(c, "--init-profile", 180)) {
      RuntimeSetup.start(c);
      return false;
    }
    boolean previous = c.getSharedPreferences("config", 0).getBoolean("mainline_kernel", false);
    c.getSharedPreferences("config", 0).edit().putBoolean("mainline_kernel", mainline).apply();
    if ("ready".equals(RuntimeSetup.start(c))) return true;
    RuntimeSetup.stop(c);
    c.getSharedPreferences("config", 0).edit().putBoolean("mainline_kernel", previous).apply();
    RuntimeSetup.start(c);
    return false;
  }
}
