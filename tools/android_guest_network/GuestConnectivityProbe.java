package net.ryuya_dev.protectedlinux.guestnetwork;

import android.os.Build;
import android.os.Process;
import java.net.HttpURLConnection;
import java.net.URI;

/** One-shot guest-only HTTPS checks. Normal certificate verification stays enabled. */
public final class GuestConnectivityProbe {
  public static void main(String[] args) {
    if (Process.myUid() != 0
        || args.length != 0
        || !Build.DEVICE.equals("vsoc_arm64_only")
        || !Build.HARDWARE.equals("cutf_cvm")
        || !Build.VERSION.INCREMENTAL.equals("16373615")) {
      System.exit(20);
    }
    int passed = 0;
    for (String address :
        new String[] {
          "https://example.com/", "https://www.google.com/generate_204", "https://auroraoss.com/"
        }) {
      HttpURLConnection connection = null;
      long start = System.nanoTime();
      try {
        connection = (HttpURLConnection) URI.create(address).toURL().openConnection();
        connection.setConnectTimeout(8000);
        connection.setReadTimeout(8000);
        connection.setInstanceFollowRedirects(false);
        connection.setRequestProperty("Connection", "close");
        int status = connection.getResponseCode();
        System.out.println(
            "PLB_GUEST_HTTPS host="
                + URI.create(address).getHost()
                + " status="
                + status
                + " elapsed_ms="
                + (System.nanoTime() - start) / 1000000);
        if (status >= 200 && status < 400) passed++;
      } catch (Exception error) {
        System.out.println(
            "PLB_GUEST_HTTPS_FAILED host="
                + URI.create(address).getHost()
                + " error="
                + error.getClass().getSimpleName()
                + " message="
                + error.getMessage());
      } finally {
        if (connection != null) connection.disconnect();
      }
    }
    System.out.println("PLB_GUEST_HTTPS_COMPLETE passed=" + passed + " total=3");
    System.exit(passed == 3 ? 0 : 21);
  }
}
