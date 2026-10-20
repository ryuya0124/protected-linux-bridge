package net.ryuya_dev.protectedlinux;

/** Android 17 branch only. ROM inspection is distinct from device validation. */
final class HostContract {
  static final int SDK = 37;
  static final long TERMINAL_VERSION = 37;
  static final String MODEL = "SM-S948Q";
  static final String BUILD = "S948QOPU1BZI9";
  static final String KERNEL = "6.12.69-android16-6-pee899be-abogkiS948QOPU1BZI9-4k";
  static final String TERMINAL_SHA256 =
      "e493cb1ad8798e3a2c0320c6edd5c909ecf963fb4a37f165cb3de57015705c55";
  // The inspected APEX has no libgfxstream_backend.so. Old GPU bundles are incompatible.
  static final boolean ANDROID_GUEST_READY = false;

  static boolean matches(int sdk, String model, String build, String kernel, long terminal) {
    return sdk == SDK
        && MODEL.equals(model)
        && BUILD.equals(build)
        && KERNEL.equals(kernel)
        && terminal == TERMINAL_VERSION;
  }

  private HostContract() {}
}
