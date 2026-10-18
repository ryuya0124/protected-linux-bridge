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

  static File install(Context context) throws Exception {
    File directory = new File(context.getFilesDir(), "runtime");
    if (!directory.isDirectory() && !directory.mkdirs()) throw new java.io.IOException("runtime");
    File pending = new File(directory, "android-bridge.pending");
    try (InputStream source = context.getAssets().open("runtime/android-bridge")) {
      Files.copy(source, pending.toPath(), StandardCopyOption.REPLACE_EXISTING);
    }
    if (!pending.setReadable(false, false)
        || !pending.setReadable(true, true)
        || !pending.setExecutable(true, true)) throw new java.io.IOException("permissions");
    File executable = new File(directory, "android-bridge");
    Files.move(
        pending.toPath(),
        executable.toPath(),
        StandardCopyOption.ATOMIC_MOVE,
        StandardCopyOption.REPLACE_EXISTING);
    return executable;
  }

  static int errorMessage(int code) {
    return switch (code) {
      case 21 -> R.string.android_guest_missing;
      case 23 -> R.string.android_guest_headroom;
      case 28, 30 -> R.string.android_guest_busy;
      case 31 -> R.string.android_guest_integrity;
      default -> R.string.android_guest_root_error;
    };
  }

  static Process start(Context context) throws Exception {
    if (android.os.Build.VERSION.SDK_INT != 36 || !android.os.Build.MODEL.equals("SM-S948Q"))
      throw new java.io.IOException(context.getString(R.string.android_guest_unsupported));
    File executable = install(context);
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
    if (!RuntimeSetup.stop(context))
      throw new java.io.IOException(context.getString(R.string.android_guest_busy));
    return new ProcessBuilder(
            "su", "-M", "-c", "exec " + executable + " " + android.os.Process.myUid())
        .redirectErrorStream(true)
        .start();
  }
}
