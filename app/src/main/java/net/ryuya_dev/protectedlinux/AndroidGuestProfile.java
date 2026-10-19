package net.ryuya_dev.protectedlinux;

/** Exact selectable Android presets. No value implies a successful guest boot. */
final class AndroidGuestProfile {
  private static final int[] MEMORY_CHOICES = {1536, 1792, 512};

  private AndroidGuestProfile() {}

  static boolean supported(int memoryMib) {
    return memoryMib == 512 || memoryMib == 1536 || memoryMib == 1792;
  }

  static boolean guarded(int memoryMib) {
    return memoryMib == 1536 || memoryMib == 1792;
  }

  static int memoryAt(int position) {
    if (position < 0 || position >= MEMORY_CHOICES.length)
      throw new IllegalArgumentException("Android memory selection");
    return MEMORY_CHOICES[position];
  }

  static String executable(int memoryMib) {
    return switch (memoryMib) {
      case 512 -> "android-bridge";
      case 1536 -> "android-large-bridge";
      case 1792 -> "android-large1792-bridge";
      default -> throw new IllegalArgumentException("Android memory preset");
    };
  }

  static String library(int memoryMib) {
    return switch (memoryMib) {
      case 1536 -> "libandroid-memory.so";
      case 1792 -> "libandroid-memory1792.so";
      default -> throw new IllegalArgumentException("Android memory library");
    };
  }
}
