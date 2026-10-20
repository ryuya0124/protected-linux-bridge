package net.ryuya_dev.protectedlinux;

import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReferenceArray;

/** One private launch pipe. Keeps one reading per kind; never blocks SensorManager callbacks. */
final class AndroidGuestSensorPipe implements AutoCloseable {
  private final AtomicReferenceArray<AndroidGuestSensorSource.Reading> latest =
      new AtomicReferenceArray<>(9);
  private final AtomicBoolean closed = new AtomicBoolean();
  private final AtomicLong written = new AtomicLong();
  private volatile IOException failure;
  private Thread writer;

  void offer(AndroidGuestSensorSource.Reading reading) {
    if (!closed.get()) latest.set(reading.kind().guestId, reading);
  }

  synchronized void start(OutputStream pipe, int selectedMask) throws IOException {
    if (closed.get() || writer != null || selectedMask == 0 || (selectedMask & ~0x1f7) != 0)
      throw new IOException("Sensor pipe state");
    DataOutputStream output = new DataOutputStream(pipe);
    // Written immediately after the unchanged 40-byte display capability.
    output.writeInt(0x504c5331);
    output.writeInt(selectedMask);
    output.flush();
    writer = new Thread(() -> stream(output), "owned-guest-sensor-pipe");
    writer.setDaemon(true);
    writer.start();
  }

  private void stream(DataOutputStream output) {
    try {
      while (!closed.get()) {
        for (int kind = 0; kind < latest.length(); kind++) {
          AndroidGuestSensorSource.Reading reading = latest.getAndSet(kind, null);
          if (reading == null) continue;
          float[] values = reading.values();
          output.writeInt(reading.kind().guestId);
          output.writeInt(values.length);
          output.writeLong(reading.hostTimestampNs());
          for (int i = 0; i < 3; i++) output.writeFloat(i < values.length ? values[i] : 0f);
          output.writeInt(0);
          output.flush();
          written.incrementAndGet();
        }
        Thread.sleep(20);
      }
    } catch (IOException error) {
      if (!closed.get()) failure = error;
    } catch (InterruptedException interrupted) {
      Thread.currentThread().interrupt();
    } finally {
      clear();
    }
  }

  long written() {
    return written.get();
  }

  IOException failure() {
    return failure;
  }

  @Override
  public synchronized void close() {
    closed.set(true);
    clear();
    if (writer != null) writer.interrupt();
    // The session owner terminates the producer and closes its stdin. A blocked
    // write then fails; no callback thread or growing event queue is retained.
  }

  private void clear() {
    for (int kind = 0; kind < latest.length(); kind++) latest.set(kind, null);
  }
}
