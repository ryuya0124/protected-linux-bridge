package net.ryuya_dev.protectedlinux;

/** Stable mode IDs; rotation swaps dimensions without changing pixel count. */
final class DisplaySizing {
  private static final int[][] MODES = {
    {960, 540},
    {1024, 768},
    {1280, 720},
    {1280, 800},
    {1600, 900},
    {1920, 1080},
    {2560, 1440},
    {720, 1280},
    {1080, 1920}
  };

  static int[] dimensions(int id, boolean landscape) {
    if (id < 0 || id >= MODES.length) id = 2;
    int a = MODES[id][0], b = MODES[id][1];
    return landscape
        ? new int[] {Math.max(a, b), Math.min(a, b)}
        : new int[] {Math.min(a, b), Math.max(a, b)};
  }
}
