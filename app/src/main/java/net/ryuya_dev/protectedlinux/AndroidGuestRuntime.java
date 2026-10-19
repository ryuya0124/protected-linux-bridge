package net.ryuya_dev.protectedlinux;

import android.content.Context;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.concurrent.TimeUnit;

/** Own executable and guest profile only. Never starts or stops host Android services. */
final class AndroidGuestRuntime {
  private AndroidGuestRuntime() {}

  static File install(Context context, String name) throws Exception {
    File directory = new File(context.getFilesDir(), "runtime");
    if (!directory.isDirectory() && !directory.mkdirs()) throw new java.io.IOException("runtime");
    File pending = new File(directory, name + ".pending");
    try (InputStream source = context.getAssets().open("runtime/" + name)) {
      Files.copy(source, pending.toPath(), StandardCopyOption.REPLACE_EXISTING);
    }
    if (!pending.setReadable(false, false)
        || !pending.setReadable(true, true)
        || !pending.setExecutable(true, true)) throw new java.io.IOException("permissions");
    File executable = new File(directory, name);
    Files.move(
        pending.toPath(),
        executable.toPath(),
        StandardCopyOption.ATOMIC_MOVE,
        StandardCopyOption.REPLACE_EXISTING);
    return executable;
  }

  static boolean verifiedCheck(int result, String output, int memoryMib) {
    return result == 0
        && (memoryMib == 512 || memoryMib == 1536)
        && output != null
        && output.length() < 4096
        && output.trim().equals("ANDROID_PROFILE_CHECK_OK memory_mib=" + memoryMib);
  }

  static int errorMessage(int code) {
    return switch (code) {
      case 21 -> R.string.android_guest_missing;
      case 23 -> R.string.android_guest_headroom;
      case 28, 30 -> R.string.android_guest_busy;
      case 31 -> R.string.android_guest_integrity;
      case 35 -> R.string.android_guest_profile_copy_failed;
      case 38 -> R.string.android_guest_policy_failed;
      case 39 -> R.string.android_guest_large_unsupported;
      case 40 -> R.string.android_guest_preparation_failed;
      case 41 -> R.string.android_guest_process_isolation_failed;
      case 89 -> R.string.android_guest_fragmented;
      case 124 -> R.string.android_guest_session_ended;
      default -> R.string.android_guest_root_error;
    };
  }

  static Process start(Context context, int memoryMib, boolean policyAcknowledged)
      throws Exception {
    if (android.os.Build.VERSION.SDK_INT != 36 || !android.os.Build.MODEL.equals("SM-S948Q"))
      throw new java.io.IOException(context.getString(R.string.android_guest_unsupported));
    boolean large = memoryMib == 1536;
    if ((memoryMib != 512 && !large) || (large && !policyAcknowledged))
      throw new java.io.IOException(context.getString(R.string.android_guest_policy_consent));
    if (large) install(context, "libandroid-memory.so");
    File executable = install(context, large ? "android-large-bridge" : "android-bridge");
    Process check =
        new ProcessBuilder("su", "-M", "-c", executable + " --check")
            .redirectErrorStream(true)
            .start();
    if (!check.waitFor(15, TimeUnit.SECONDS)) {
      check.destroy();
      throw new java.io.IOException(context.getString(R.string.android_guest_root_error));
    }
    if (check.exitValue() != 0)
      throw new java.io.IOException(context.getString(errorMessage(check.exitValue())));
    String checked =
        new String(
            check.getInputStream().readNBytes(4096), java.nio.charset.StandardCharsets.UTF_8);
    if (!verifiedCheck(check.exitValue(), checked, memoryMib))
      throw new java.io.IOException(context.getString(R.string.android_guest_root_error));
    if (!RuntimeSetup.stop(context))
      throw new java.io.IOException(context.getString(R.string.android_guest_busy));
    return new ProcessBuilder(
            "su",
            "-M",
            "-c",
            "exec "
                + executable
                + " "
                + android.os.Process.myUid()
                + (large ? " --ack-small-shmem" : ""))
        .redirectErrorStream(true)
        .start();
  }
}
