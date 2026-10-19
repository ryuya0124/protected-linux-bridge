package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.os.Build;
import android.os.Process;
import java.lang.reflect.Modifier;

/** Read-only framework metadata in the pinned Android guest, never the host. */
public final class GuestNetworkInspector {
  public static void main(String[] args) {
    if (Process.myUid() != 0
        || args.length != 0
        || !Build.DEVICE.equals("vsoc_arm64_only")
        || !Build.HARDWARE.equals("cutf_cvm")
        || !Build.VERSION.INCREMENTAL.equals("16373615")) {
      System.exit(20);
    }
    for (String name :
        new String[] {
          "android.net.NetworkAgent", "android.net.NetworkAgentConfig$Builder",
          "android.net.NetworkScore$Builder", "android.net.NetworkProvider"
        }) {
      try {
        Class<?> type = Class.forName(name);
        System.out.println(
            "PLB_GUEST_NETWORK_CLASS " + name + " " + Modifier.toString(type.getModifiers()));
        for (var constructor : type.getDeclaredConstructors()) System.out.println(constructor);
        for (var method : type.getDeclaredMethods()) {
          if (Modifier.isPublic(method.getModifiers())
              || Modifier.isAbstract(method.getModifiers())) System.out.println(method);
        }
      } catch (Throwable error) {
        System.out.println("PLB_GUEST_NETWORK_METADATA_FAILED " + error);
        System.exit(21);
      }
    }
    System.out.println("PLB_GUEST_NETWORK_METADATA_OK");
  }
}
