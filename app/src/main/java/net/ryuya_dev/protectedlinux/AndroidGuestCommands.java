package net.ryuya_dev.protectedlinux;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.concurrent.TimeUnit;

/** Bounded input queue. Coalesce only adjacent MOVE samples, preserving gesture edges. */
final class AndroidGuestCommands {
  static final int CAPACITY = 64;
  private final boolean pointers;

  AndroidGuestCommands(boolean pointers) {
    this.pointers = pointers;
  }

  private final ArrayDeque<byte[]> queue = new ArrayDeque<>();

  private static boolean motion(byte[] packet) {
    if (packet.length < 21 || packet[0] != 'V') return false;
    ByteBuffer data = ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN);
    int count = data.getInt(1);
    if (count < 1 || count > 10 || packet.length != 5 + count * 16) return false;
    for (int i = 0; i < count; i++) if (data.getInt(9 + i * 16) != 2) return false;
    return true;
  }

  synchronized boolean offer(byte[] packet) {
    if (motion(packet) && !queue.isEmpty() && motion(queue.peekLast())) queue.removeLast();
    if (queue.size() >= CAPACITY) queue.removeIf(AndroidGuestCommands::motion);
    if (queue.size() >= CAPACITY) {
      if (!pointers) return false;
      // Overflow must release any pointer already delivered, even when an UP
      // cannot be queued. Do not replay stale gesture edges after cancellation.
      queue.clear();
      ByteBuffer cancel = ByteBuffer.allocate(165).order(ByteOrder.LITTLE_ENDIAN);
      cancel.put((byte) 'V').putInt(10);
      for (int i = 0; i < 10; i++) cancel.putInt(i).putInt(3).putInt(0).putInt(0);
      queue.add(cancel.array());
      notifyAll();
      return false;
    }
    queue.add(packet);
    notifyAll();
    return true;
  }

  synchronized byte[] poll() {
    return queue.poll();
  }

  synchronized byte[] poll(long timeout, TimeUnit unit) throws InterruptedException {
    long remaining = unit.toNanos(timeout), deadline = System.nanoTime() + remaining;
    while (queue.isEmpty() && remaining > 0) {
      TimeUnit.NANOSECONDS.timedWait(this, remaining);
      remaining = deadline - System.nanoTime();
    }
    return queue.poll();
  }
}
