package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.junit.Test;

public class AndroidGuestCommandsTest {
  private static byte[] point(int action, int x) {
    return ByteBuffer.allocate(21)
        .order(ByteOrder.LITTLE_ENDIAN)
        .put((byte) 'V')
        .putInt(1)
        .putInt(0)
        .putInt(action)
        .putInt(x)
        .putInt(10)
        .array();
  }

  @Test
  public void onlyAdjacentMoveSamplesAreCoalesced() {
    AndroidGuestCommands queue = new AndroidGuestCommands(true);
    byte[] down = point(0, 10), move = point(2, 30), up = point(1, 40);
    assertTrue(queue.offer(down));
    assertTrue(queue.offer(point(2, 20)));
    assertTrue(queue.offer(move));
    assertTrue(queue.offer(up));
    assertArrayEquals(down, queue.poll());
    assertArrayEquals(move, queue.poll());
    assertArrayEquals(up, queue.poll());
    assertNull(queue.poll());
  }

  @Test
  public void overflowReleasesPointersAndLegacyModeNeverSendsVirtioCommands() {
    AndroidGuestCommands pointers = new AndroidGuestCommands(true);
    AndroidGuestCommands legacy = new AndroidGuestCommands(false);
    for (int i = 0; i < AndroidGuestCommands.CAPACITY; i++) {
      assertTrue(pointers.offer(new byte[] {'K'}));
      assertTrue(legacy.offer(new byte[] {'K'}));
    }
    assertFalse(pointers.offer(point(1, 10)));
    ByteBuffer cancel = ByteBuffer.wrap(pointers.poll()).order(ByteOrder.LITTLE_ENDIAN);
    assertEquals('V', cancel.get());
    assertEquals(10, cancel.getInt());
    for (int i = 0; i < 10; i++) {
      assertEquals(i, cancel.getInt());
      assertEquals(3, cancel.getInt());
      assertEquals(0, cancel.getInt());
      assertEquals(0, cancel.getInt());
    }
    assertNull(pointers.poll());
    assertFalse(legacy.offer(new byte[] {'K'}));
    for (int i = 0; i < AndroidGuestCommands.CAPACITY; i++)
      assertArrayEquals(new byte[] {'K'}, legacy.poll());
    assertNull(legacy.poll());
  }

  @Test
  public void inputWakesAWaitingWorkerWithoutWaitingForItsPingDeadline() throws Exception {
    AndroidGuestCommands queue = new AndroidGuestCommands(true);
    var worker = Executors.newSingleThreadExecutor();
    try {
      var received = worker.submit(() -> queue.poll(5, TimeUnit.SECONDS));
      byte[] down = point(0, 10);
      assertTrue(queue.offer(down));
      assertArrayEquals(down, received.get(1, TimeUnit.SECONDS));
    } finally {
      worker.shutdownNow();
    }
  }
}
