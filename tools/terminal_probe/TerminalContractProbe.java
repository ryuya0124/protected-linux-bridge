package net.ryuya_dev.protectedlinux;

import android.os.Build;
import android.system.Os;
import dalvik.system.PathClassLoader;
import java.io.*;
import java.security.MessageDigest;
import java.util.HexFormat;

/** Shell-process reflection only: no Activity, VM, hooks, root request or GUI operation. */
public final class TerminalContractProbe {
  public static void main(String[] args) {
    try {
      if (args.length != 1
          || !args[0].startsWith("/apex/com.android.virt/priv-app/")
          || !args[0].endsWith("/VmTerminalApp.apk")
          || args[0].contains("..")) throw new SecurityException("Terminal path");
      if (!HostContract.matches(
          Build.VERSION.SDK_INT,
          Build.MODEL,
          Build.VERSION.INCREMENTAL,
          Os.uname().release,
          HostContract.TERMINAL_VERSION)) throw new SecurityException("Terminal host");
      MessageDigest digest = MessageDigest.getInstance("SHA-256");
      try (FileInputStream stream = new FileInputStream(args[0])) {
        byte[] buffer = new byte[65536];
        int length;
        while ((length = stream.read(buffer)) != -1) digest.update(buffer, 0, length);
      }
      if (!HostContract.TERMINAL_SHA256.equals(HexFormat.of().formatHex(digest.digest())))
        throw new SecurityException("Terminal identity");
      PathClassLoader loader = new PathClassLoader(args[0], ClassLoader.getSystemClassLoader());
      String prefix = "com.android.virtualization.terminal.";
      TerminalContract.Methods methods =
          TerminalContract.inspect(
              Class.forName(prefix + "MainActivity", false, loader),
              Class.forName(prefix + "TerminalTabFragment", false, loader),
              android.view.View.class,
              android.webkit.WebView.class,
              android.os.Bundle.class,
              android.app.Activity.class);
      System.out.println(
          "TERMINAL_CONTRACT_OK sdk=37 view_teardown="
              + methods.viewDestroyed().getDeclaringClass().getName()
              + " deopt_callers="
              + methods.callers().size());
    } catch (Throwable failure) {
      System.err.println("TERMINAL_CONTRACT_REFUSED " + failure.getClass().getSimpleName());
      System.exit(41);
    }
  }
}
