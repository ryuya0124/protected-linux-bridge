package net.ryuya_dev.protectedlinux;

import android.content.Context;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

/** PackageManager data changes only. Never replaces an installed Linux image. */
final class TerminalSetup {
  enum Result {
    ENABLED,
    OPENED,
    ALREADY_INSTALLED,
    FAILED
  }

  private static final String PREFIX = "PLB_TERMINAL_SETUP ";

  static Result run(Context context, boolean openInstaller) {
    Process process = null;
    var reader = Executors.newSingleThreadExecutor();
    try {
      if (!HostPlatform.compatible(context)) return Result.FAILED;
      process =
          new ProcessBuilder("su", "-M", "-c", command(openInstaller))
              .redirectErrorStream(true)
              .start();
      Process current = process;
      var output = reader.submit(() -> current.getInputStream().readNBytes(65537));
      byte[] bytes = output.get(20, TimeUnit.SECONDS);
      if (bytes.length > 65536 || !process.waitFor(1, TimeUnit.SECONDS)) return Result.FAILED;
      Result result = parse(process.exitValue(), new String(bytes, StandardCharsets.UTF_8));
      android.util.Log.i("ProtectedLinux", "TERMINAL_SETUP " + result.name());
      return result;
    } catch (InterruptedException error) {
      Thread.currentThread().interrupt();
      return Result.FAILED;
    } catch (Exception error) {
      android.util.Log.w(
          "ProtectedLinux", "TERMINAL_SETUP_FAILED " + error.getClass().getSimpleName());
      return Result.FAILED;
    } finally {
      if (process != null) {
        if (process.isAlive()) process.destroyForcibly();
        try {
          process.getInputStream().close();
        } catch (Exception ignored) {
        }
      }
      reader.shutdownNow();
    }
  }

  // This is the inspected OTHER package's primary-user data, queried by root.
  // Context.getFilesDir() would point at Bridge and test the wrong image.
  @android.annotation.SuppressLint("SdCardPath")
  static String command(boolean openInstaller) {
    String host = RuntimeSetup.HOST;
    String enable =
        "pm enable --user 0 "
            + host
            + " && pm enable --user 0 "
            + host
            + "/.MainActivity"
            + " && pm enable --user 0 "
            + host
            + "/.InstallerActivity";
    if (!openInstaller) return enable + " && echo '" + PREFIX + "ENABLED'";
    // The inspected InstalledImage.isInstalled() checks this completion marker,
    // not root_part existence/size. An existing image must never be removed to
    // keep the stock installer visible: it intentionally finishes with RESULT_OK.
    String marker = "/data/user/0/" + host + "/files/linux/completed";
    return enable
        + " && if [ -e "
        + marker
        + " ]; then echo '"
        + PREFIX
        + "ALREADY_INSTALLED'; else am start -W --user 0 -n "
        + host
        + "/.InstallerActivity && if [ -e "
        + marker
        + " ]; then echo '"
        + PREFIX
        + "ALREADY_INSTALLED'; else echo '"
        + PREFIX
        + "OPENED'; fi; fi";
  }

  static Result parse(int exitCode, String output) {
    if (exitCode != 0) return Result.FAILED;
    Result found = Result.FAILED;
    int receipts = 0;
    boolean launched = false;
    for (String line : output.split("\\R")) {
      line = line.strip();
      if (line.startsWith("Error:") || line.contains("Exception")) return Result.FAILED;
      if (line.equals("Status: ok")) launched = true;
      if (!line.startsWith(PREFIX)) continue;
      receipts++;
      try {
        found = Result.valueOf(line.substring(PREFIX.length()));
      } catch (IllegalArgumentException unknown) {
        return Result.FAILED;
      }
    }
    // Some root managers return exit 0 on denial; require one positive receipt.
    if (receipts != 1 || (found == Result.OPENED && !launched)) return Result.FAILED;
    return found;
  }
}
