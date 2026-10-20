package net.ryuya_dev.protectedlinux;

import android.content.Context;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;
import org.json.JSONObject;

/** An independent, read-only root query: does not prepare or stop a VM. */
final class AssetInventory {
  private AssetInventory() {}

  static JSONObject read(Context context) throws Exception {
    int uid = android.os.Process.myUid();
    if (uid < 10000 || uid > 19999) throw new java.io.IOException("primary user required");
    String executable = AndroidGuestRuntime.install(context, "asset-inventory").getAbsolutePath();
    Process process =
        new ProcessBuilder("su", "-M", "-c", executable).redirectErrorStream(true).start();
    ExecutorService reader = Executors.newSingleThreadExecutor();
    try {
      byte[] output =
          reader.submit(() -> process.getInputStream().readNBytes(65537)).get(20, TimeUnit.SECONDS);
      if (output.length > 65536
          || !process.waitFor(2, TimeUnit.SECONDS)
          || process.exitValue() != 0) throw new java.io.IOException("inventory unavailable");
      JSONObject result = new JSONObject(new String(output, StandardCharsets.UTF_8).trim());
      if (result.getInt("schema") != 1 || result.getJSONArray("items").length() != 8)
        throw new java.io.IOException("inventory contract");
      return result;
    } finally {
      process.destroyForcibly();
      reader.shutdownNow();
    }
  }
}
