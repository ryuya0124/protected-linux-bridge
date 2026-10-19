package net.ryuya_dev.protectedlinux;

/** Maps a single finger through the letterboxed viewer without turning swipes into taps. */
final class AndroidGuestTouch {
  private int startX, startY;
  private long startedAt;
  private boolean active;

  private static float scale(int width, int height) {
    return Math.min(width / 480f, height / 800f);
  }

  boolean begin(float x, float y, int width, int height, long time) {
    cancel();
    float scale = scale(width, height);
    if (scale <= 0) return false;
    float guestX = (x - (width - 480 * scale) / 2) / scale;
    float guestY = (y - (height - 800 * scale) / 2) / scale;
    if (guestX < 0 || guestX >= 480 || guestY < 0 || guestY >= 800) return false;
    startX = (int) guestX;
    startY = (int) guestY;
    startedAt = time;
    active = true;
    return true;
  }

  int[] end(float x, float y, int width, int height, long time, int touchSlop) {
    if (!active) return null;
    cancel();
    float scale = scale(width, height);
    if (scale <= 0) return null;
    int endX = Math.max(0, Math.min(479, (int) ((x - (width - 480 * scale) / 2) / scale)));
    int endY = Math.max(0, Math.min(799, (int) ((y - (height - 800 * scale) / 2) / scale)));
    float dx = (endX - startX) * scale;
    float dy = (endY - startY) * scale;
    if (dx * dx + dy * dy <= (float) touchSlop * touchSlop) {
      return new int[] {startX, startY};
    }
    int duration = (int) Math.max(50, Math.min(1000, time - startedAt));
    return new int[] {startX, startY, endX, endY, duration};
  }

  void cancel() {
    active = false;
  }
}
