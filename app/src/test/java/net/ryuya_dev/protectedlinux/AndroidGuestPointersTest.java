package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.Test;

public class AndroidGuestPointersTest {
  private static int value(byte[] packet, int index) {
    assertEquals('V', packet[0]);
    return ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN).getInt(1 + index * 4);
  }

  @Test
  public void reorderedHostIdsPreserveSlotsAndBatchMoves() {
    AndroidGuestPointers state = new AndroidGuestPointers();
    byte[] first =
        state.sample(0, 0, new int[] {42}, new float[] {100}, new float[] {200}, 960, 1600);
    assertEquals(0, value(first, 1));
    assertEquals(50, value(first, 3));
    byte[] second =
        state.sample(
            5, 1, new int[] {42, 7}, new float[] {100, 300}, new float[] {200, 400}, 960, 1600);
    assertEquals(1, value(second, 1));
    byte[] move =
        state.sample(
            2, 0, new int[] {7, 42}, new float[] {350, 150}, new float[] {450, 250}, 960, 1600);
    assertEquals(2, value(move, 0));
    assertEquals(1, value(move, 1));
    assertEquals(0, value(move, 5));
    assertEquals(2, value(move, 2));
    assertEquals(175, value(move, 3));
    state.sample(
        6, 0, new int[] {7, 42}, new float[] {350, 150}, new float[] {450, 250}, 960, 1600);
    byte[] remaining =
        state.sample(2, 0, new int[] {42}, new float[] {160}, new float[] {260}, 960, 1600);
    assertEquals(0, value(remaining, 1));
    byte[] reused =
        state.sample(
            5, 1, new int[] {42, 99}, new float[] {160, 300}, new float[] {260, 400}, 960, 1600);
    assertEquals(1, value(reused, 1));
    byte[] cancel = state.cancel();
    assertEquals(2, value(cancel, 0));
    assertEquals(3, value(cancel, 2));
    assertEquals(3, value(cancel, 6));
    assertNull(state.cancel());
  }

  @Test
  public void tenFingersAndCancellationOfExcessOrInvalidInput() {
    AndroidGuestPointers state = new AndroidGuestPointers();
    for (int i = 1; i <= 10; i++) {
      int[] ids = new int[i];
      float[] x = new float[i], y = new float[i];
      for (int j = 0; j < i; j++) {
        ids[j] = 100 + j;
        x[j] = 20 + j;
        y[j] = 40 + j;
      }
      byte[] down = state.sample(i == 1 ? 0 : 5, i - 1, ids, x, y, 480, 800);
      assertEquals(i - 1, value(down, 1));
    }
    byte[] cancelled = state.sample(2, 0, new int[11], new float[11], new float[11], 480, 800);
    assertEquals(10, value(cancelled, 0));
    assertNull(state.cancel());
    assertNull(
        state.sample(0, 0, new int[] {0}, new float[] {Float.NaN}, new float[] {0}, 480, 800));
  }

  @Test
  public void letterboxRejectionClippingAndRepeatedTaps() {
    AndroidGuestPointers state = new AndroidGuestPointers();
    assertNull(state.sample(0, 0, new int[] {0}, new float[] {5}, new float[] {300}, 1000, 800));
    for (int i = 0; i < 2; i++) {
      byte[] down =
          state.sample(0, 0, new int[] {5}, new float[] {300}, new float[] {300}, 1000, 800);
      assertEquals(40, value(down, 3));
      byte[] up =
          state.sample(1, 0, new int[] {5}, new float[] {2000}, new float[] {2000}, 1000, 800);
      assertEquals(479, value(up, 3));
      assertEquals(799, value(up, 4));
    }
    assertNull(state.cancel());
  }
}
