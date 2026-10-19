package net.ryuya_dev.protectedlinux.guestnetwork;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.IpPrefix;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.NetworkAgent;
import android.net.NetworkAgentConfig;
import android.net.NetworkCapabilities;
import android.net.RouteInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import java.net.InetAddress;

/** Guest-only ordinary network registration. No host VPN, routes or DNS changes. */
public final class GuestNetworkAgent extends NetworkAgent {
  private GuestNetworkAgent(Context context, NetworkCapabilities caps, LinkProperties links) {
    super(
        context,
        Looper.getMainLooper(),
        "ProtectedLinuxGuest",
        caps,
        links,
        70,
        new NetworkAgentConfig.Builder()
            .setLegacyType(9)
            .setLegacyTypeName("ETHERNET")
            .setLegacyExtraInfo("Bridge")
            .setExplicitlySelected(true)
            .build(),
        null);
  }

  @Override
  public void onNetworkUnwanted() {
    System.out.println("PLB_GUEST_NETWORK_UNWANTED");
    unregister();
    System.exit(0);
  }

  @Override
  public void onValidationStatus(int status, Uri redirect) {
    // Validation is supplied by Android's real probes; never assert VALIDATED.
    System.out.println("PLB_GUEST_NETWORK_VALIDATION status=" + status);
  }

  public static void main(String[] args) {
    try {
      if (Process.myUid() != 0
          || args.length != 0
          || !Build.DEVICE.equals("vsoc_arm64_only")
          || !Build.HARDWARE.equals("cutf_cvm")
          || !Build.VERSION.INCREMENTAL.equals("16373615")) System.exit(20);
      Looper.prepareMainLooper();
      Class<?> activityThread = Class.forName("android.app.ActivityThread");
      Object thread = activityThread.getMethod("systemMain").invoke(null);
      Context context = (Context) activityThread.getMethod("getSystemContext").invoke(thread);
      ConnectivityManager connectivity = context.getSystemService(ConnectivityManager.class);
      if (connectivity.getActiveNetwork() != null)
        throw new IllegalStateException("Existing network");
      if (java.net.NetworkInterface.getByName("plb0") == null)
        throw new IllegalStateException("Guest TUN missing");
      NetworkCapabilities caps = new NetworkCapabilities();
      NetworkCapabilities.class
          .getMethod("addTransportType", int.class)
          .invoke(caps, NetworkCapabilities.TRANSPORT_ETHERNET);
      for (String name :
          new String[] {
            "NET_CAPABILITY_INTERNET",
            "NET_CAPABILITY_NOT_RESTRICTED",
            "NET_CAPABILITY_TRUSTED",
            "NET_CAPABILITY_NOT_VPN",
            "NET_CAPABILITY_NOT_VCN_MANAGED"
          }) {
        NetworkCapabilities.class
            .getMethod("addCapability", int.class)
            .invoke(caps, NetworkCapabilities.class.getField(name).getInt(null));
      }
      LinkProperties links = new LinkProperties();
      links.setInterfaceName("plb0");
      links.setMtu(1280);
      links.setLinkAddresses(
          java.util.List.of(
              LinkAddress.class.getConstructor(String.class).newInstance("198.18.0.1/32")));
      links.addRoute(
          RouteInfo.class
              .getConstructor(IpPrefix.class, InetAddress.class, String.class)
              .newInstance(new IpPrefix(InetAddress.getByName("0.0.0.0"), 0), null, "plb0"));
      links.setDnsServers(
          java.util.List.of(InetAddress.getByName("1.1.1.1"), InetAddress.getByName("8.8.8.8")));
      GuestNetworkAgent agent = new GuestNetworkAgent(context, caps, links);
      System.out.println("PLB_GUEST_NETWORK_REGISTERED " + agent.register());
      agent.markConnected();
      new Handler(Looper.getMainLooper())
          .postDelayed(
              () -> {
                agent.unregister();
                System.exit(0);
              },
              30 * 60 * 1000);
      Looper.loop();
    } catch (Throwable error) {
      error.printStackTrace(System.out);
      System.exit(21);
    }
  }
}
