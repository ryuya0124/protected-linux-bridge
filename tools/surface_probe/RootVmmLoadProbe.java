package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.os.Build;
import android.os.Process;
import android.system.Os;
import android.system.OsConstants;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.security.MessageDigest;

/** Load only the fixed experimental VMM and query its help; never starts a VM. */
public final class RootVmmLoadProbe {
  static final String BASE = "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/";

  private static native int check(FileDescriptor descriptor);

  static void verifyHost() {
    if (Process.myUid() != 0
        || !Build.MODEL.equals("SM-S948Q")
        || !Build.VERSION.INCREMENTAL.equals("S948QOPS1AZHL"))
      throw new SecurityException("Pinned root host only");
  }

  private static String digest(byte[] bytes) {
    StringBuilder result = new StringBuilder();
    for (byte value : bytes) result.append(String.format("%02x", value & 255));
    return result.toString();
  }

  static FileDescriptor openLibrary() throws Exception {
    FileDescriptor descriptor =
        Os.open(
            BASE + "libcrosvm-surface.so",
            OsConstants.O_RDONLY | OsConstants.O_NOFOLLOW | OsConstants.O_CLOEXEC,
            0);
    try (FileInputStream input = new FileInputStream(descriptor)) {
      var stat = Os.fstat(descriptor);
      if (!OsConstants.S_ISREG(stat.st_mode)
          || stat.st_uid != 0
          || stat.st_nlink != 1
          || (stat.st_mode & 0777) != 0500
          || stat.st_size < 1024
          || stat.st_size > 32 * 1024 * 1024) throw new SecurityException("VMM ownership/size");
      MessageDigest hash = MessageDigest.getInstance("SHA-256");
      byte[] chunk = new byte[65536];
      int size;
      while ((size = input.read(chunk)) != -1) hash.update(chunk, 0, size);
      if (!digest(hash.digest()).equals("@VMM_SHA256@")) throw new SecurityException("VMM digest");
      Os.lseek(descriptor, 0, OsConstants.SEEK_SET);
      return Os.dup(descriptor);
    }
  }

  public static void main(String[] args) {
    try {
      verifyHost();
      if (args.length != 0) System.exit(20);
      FileDescriptor descriptor = openLibrary();
      try {
        System.load(BASE + "libsurface-probe.so");
        int code = check(descriptor);
        System.out.println("PLB_SURFACE_VMM_HELP_EXIT " + code);
        System.exit(code);
      } finally {
        Os.close(descriptor);
      }
    } catch (Throwable error) {
      error.printStackTrace(System.out);
      System.exit(30);
    }
  }
}
