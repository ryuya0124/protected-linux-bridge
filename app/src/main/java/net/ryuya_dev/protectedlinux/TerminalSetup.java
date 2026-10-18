package net.ryuya_dev.protectedlinux;

import java.util.concurrent.TimeUnit;

/** PackageManager data changes only. Never installs an APK or modifies the Terminal image. */
final class TerminalSetup {
  static boolean run(boolean openInstaller) {
    Process process = null;
    try {
      String host = RuntimeSetup.HOST;
      String command =
          "pm enable --user 0 "
              + host
              + " && pm enable --user 0 "
              + host
              + "/.MainActivity"
              + " && pm enable --user 0 "
              + host
              + "/.InstallerActivity";
      if (openInstaller) command += " && am start --user 0 -n " + host + "/.InstallerActivity";
      process = new ProcessBuilder("su", "-M", "-c", command).redirectErrorStream(true).start();
      if (!process.waitFor(15, TimeUnit.SECONDS)) return false;
      return process.exitValue() == 0;
    } catch (Exception error) {
      android.util.Log.w(
          "ProtectedLinux", "TERMINAL_SETUP_FAILED " + error.getClass().getSimpleName());
      return false;
    } finally {
      if (process != null && process.isAlive()) process.destroyForcibly();
    }
  }
}
