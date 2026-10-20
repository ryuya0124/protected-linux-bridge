package net.ryuya_dev.protectedlinux;

import android.content.Context;
import android.os.Build;
import android.system.Os;
import java.io.FileInputStream;
import java.security.MessageDigest;
import java.util.HexFormat;

final class HostPlatform {
  private HostPlatform() {}

  static boolean compatible(Context context) throws Exception {
    var terminal = context.getPackageManager().getPackageInfo(RuntimeSetup.HOST, 0);
    if (terminal.applicationInfo == null
        || terminal.applicationInfo.uid < 10000
        || terminal.applicationInfo.uid > 19999) return false;
    if (!HostContract.matches(
        Build.VERSION.SDK_INT,
        Build.MODEL,
        Build.VERSION.INCREMENTAL,
        Os.uname().release,
        terminal.getLongVersionCode())) return false;
    MessageDigest digest = MessageDigest.getInstance("SHA-256");
    try (FileInputStream input = new FileInputStream(terminal.applicationInfo.sourceDir)) {
      byte[] buffer = new byte[65536];
      int size;
      while ((size = input.read(buffer)) != -1) digest.update(buffer, 0, size);
    }
    return HostContract.TERMINAL_SHA256.equals(HexFormat.of().formatHex(digest.digest()));
  }
}
