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
    try {
      android.os.Looper.prepareMainLooper();
      Class<?> activityThread = Class.forName("android.app.ActivityThread");
      Object thread = activityThread.getMethod("systemMain").invoke(null);
      android.content.Context context =
          (android.content.Context) activityThread.getMethod("getSystemContext").invoke(thread);
      var connectivity = context.getSystemService(android.net.ConnectivityManager.class);
      var network = connectivity.getActiveNetwork();
      var capabilities = network == null ? null : connectivity.getNetworkCapabilities(network);
      var links = network == null ? null : connectivity.getLinkProperties(network);
      boolean tun = links != null && "plb0".equals(links.getInterfaceName());
      System.out.println(
          "PLB_GUEST_DEFAULT_NETWORK tun="
              + tun
              + " validated="
              + (capabilities != null
                  && capabilities.hasCapability(
                      android.net.NetworkCapabilities.NET_CAPABILITY_VALIDATED)));
      if (!tun) passed = 0;
    } catch (Exception failed) {
      System.out.println("PLB_GUEST_DEFAULT_NETWORK_FAILED " + failed.getClass().getSimpleName());
      passed = 0;
    }
    System.out.println("PLB_GUEST_HTTPS_COMPLETE passed=" + passed + " total=3");
    System.exit(passed == 3 ? 0 : 21);
  }
}
