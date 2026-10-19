package net.ryuya_dev.protectedlinux;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

/** Map host pointer IDs to ten stable guest slots, batching moved fingers into one report. */
final class AndroidGuestPointers {
  private final int[] ids = new int[10];

  AndroidGuestPointers() {
    Arrays.fill(ids, -1);
  }

  private int slot(int id) {
    for (int i = 0; i < ids.length; i++) if (ids[i] == id) return i;
    return -1;
  }

  private static int[] point(float x, float y, int width, int height, boolean clip) {
    if (width <= 0 || height <= 0 || !Float.isFinite(x) || !Float.isFinite(y)) return null;
    float scale = Math.min(width / 480f, height / 800f);
    float gx = (x - (width - 480 * scale) / 2) / scale;
    float gy = (y - (height - 800 * scale) / 2) / scale;
    if (!clip && (gx < 0 || gx >= 480 || gy < 0 || gy >= 800)) return null;
    return new int[] {Math.max(0, Math.min(479, (int) gx)), Math.max(0, Math.min(799, (int) gy))};
  }

  private static byte[] packet(int[][] entries, int count) {
    ByteBuffer buffer = ByteBuffer.allocate(5 + count * 16).order(ByteOrder.LITTLE_ENDIAN);
    buffer.put((byte) 'V').putInt(count);
    for (int i = 0; i < count; i++) for (int value : entries[i]) buffer.putInt(value);
    return buffer.array();
  }

  byte[] cancel() {
    int[][] entries = new int[10][];
    int count = 0;
    for (int i = 0; i < ids.length; i++)
      if (ids[i] != -1) entries[count++] = new int[] {i, 3, 0, 0};
    Arrays.fill(ids, -1);
    return count == 0 ? null : packet(entries, count);
  }

  byte[] sample(
      int action, int changed, int[] pointerIds, float[] x, float[] y, int width, int height) {
    int size = pointerIds.length;
    if (action == 3 || size == 0 || size > 10 || x.length != size || y.length != size)
      return cancel();
    for (int i = 0; i < size; i++) {
      if (pointerIds[i] < 0) return cancel();
      for (int j = 0; j < i; j++) if (pointerIds[i] == pointerIds[j]) return cancel();
    }
    if (action == 0 || action == 5) {
      if (changed < 0 || changed >= size || slot(pointerIds[changed]) != -1) return cancel();
      int free = slot(-1);
      int[] position = point(x[changed], y[changed], width, height, false);
      if (free < 0 || position == null) return cancel();
      ids[free] = pointerIds[changed];
      return packet(new int[][] {{free, 0, position[0], position[1]}}, 1);
    }
    if (action == 1 || action == 6) {
      if (changed < 0 || changed >= size) return cancel();
      int found = slot(pointerIds[changed]);
      int[] position = point(x[changed], y[changed], width, height, true);
      if (found < 0 || position == null) return cancel();
      ids[found] = -1;
      return packet(new int[][] {{found, 1, position[0], position[1]}}, 1);
    }
    if (action != 2) return null;
    int[][] entries = new int[10][];
    for (int i = 0; i < size; i++) {
      int found = slot(pointerIds[i]);
      int[] position = point(x[i], y[i], width, height, true);
      if (found < 0 || position == null) return cancel();
      entries[i] = new int[] {found, 2, position[0], position[1]};
    }
    return packet(entries, size);
  }
}
