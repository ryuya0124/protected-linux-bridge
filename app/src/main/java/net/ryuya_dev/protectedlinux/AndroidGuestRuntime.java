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
        && AndroidGuestProfile.supported(memoryMib)
        && output != null
        && output.length() < 4096
        && output.trim().equals("ANDROID_PROFILE_CHECK_OK memory_mib=" + memoryMib);
  }

  static int errorMessage(int code) {
    return switch (code) {
      case 79 -> R.string.host17_android_pending;
      case 21 -> R.string.android_guest_missing;
      case 23 -> R.string.android_guest_headroom;
      case 28, 30 -> R.string.android_guest_busy;
      case 31 -> R.string.android_guest_integrity;
      case 33 -> R.string.android_guest_gpu_allocation_failed;
      case 35 -> R.string.android_guest_profile_copy_failed;
      case 38 -> R.string.android_guest_policy_failed;
      case 39 -> R.string.android_guest_large_unsupported;
      case 40, 45, 46, 47, 48, 49, 50, 52 -> R.string.android_guest_preparation_failed;
      case 41 -> R.string.android_guest_process_isolation_failed;
      case 42, 90 -> R.string.android_guest_host_memory_warning;
      case 59, 88, 89 -> R.string.android_guest_fragmented;
      case 124 -> R.string.android_guest_session_ended;
      default -> R.string.android_guest_root_error;
    };
  }

  static Process start(
      Context context, int memoryMib, boolean policyAcknowledged, boolean gpuSurface)
      throws Exception {
    if (!HostContract.ANDROID17_EXPERIMENT_ENABLED || !HostPlatform.compatible(context))
      throw new java.io.IOException(context.getString(R.string.android_guest_unsupported));
    if (memoryMib != 1024 || !gpuSurface || !policyAcknowledged)
      throw new java.io.IOException(context.getString(R.string.android17_app_conditions));
    try (InputStream list = context.getAssets().open("runtime/android17-app-files.txt")) {
      String files = new String(list.readNBytes(4096), java.nio.charset.StandardCharsets.UTF_8);
      for (String name : files.split("\\R")) {
        if (!name.matches("[A-Za-z0-9_.-]{1,80}"))
          throw new java.io.IOException("Runtime manifest");
        install(context, "android17-app-" + name);
      }
    } catch (java.io.FileNotFoundException missing) {
      throw new java.io.IOException(context.getString(R.string.android17_app_bundle_missing));
    }
    // The pinned installer also takes the VM owner lease. Release only a
    // genuinely idle Debian manager before attempting either native check.
    if (!RuntimeSetup.releaseIdleForAndroid(context))
      throw new java.io.IOException(context.getString(R.string.android_guest_busy));
    String installer = new File(context.getFilesDir(), "runtime/android17-app-install").getPath();
    checkedCommand(context, installer, "ANDROID17_APP_RUNTIME_INSTALLED");
    String controller =
        new File(context.getFilesDir(), "android17-app-runtime/vm-controller").getPath();
    checkedCommand(context, controller + " --check", "ANDROID17_INPUTS_CHECK_PASS");
    // The native owner/maintenance locks refuse concurrent Debian sessions.
    // Do not stop the user's terminal or rewrite any guest OS disk implicitly.
    return new ProcessBuilder(
            "su", "-M", "-c", "exec " + controller + " --run-approved-gles-reclaim")
        .redirectErrorStream(true)
        .start();
  }

  static boolean verifiedAndroid17Check(int result, String output, String receipt) {
    return result == 0
        && output != null
        && output.length() < 4096
        && output.strip().equals(receipt);
  }

  private static void checkedCommand(Context context, String command, String receipt)
      throws Exception {
    Process process =
        new ProcessBuilder("su", "-M", "-c", command).redirectErrorStream(true).start();
    var reader = java.util.concurrent.Executors.newSingleThreadExecutor();
    try {
      var bytes = reader.submit(() -> process.getInputStream().readNBytes(4096));
      byte[] output = bytes.get(30, TimeUnit.SECONDS);
      if (!process.waitFor(1, TimeUnit.SECONDS))
        throw new java.io.IOException("Runtime check deadline");
      String text = new String(output, java.nio.charset.StandardCharsets.UTF_8);
      if (!verifiedAndroid17Check(process.exitValue(), text, receipt))
        throw new java.io.IOException(context.getString(errorMessage(process.exitValue())));
    } finally {
      if (process.isAlive()) process.destroy();
      process.getInputStream().close();
      reader.shutdownNow();
    }
  }
}
