package net.ryuya_dev.protectedlinux;

import java.util.function.BooleanSupplier;
import java.util.function.LongSupplier;

/** Fixed VM commands, explicit session consent and a short hardware-use lease. No root. */
final class VmTorchLease implements AutoCloseable {
  static final int VERSION = 1, REQUEST_BYTES = 4, RESPONSE_BYTES = 8;
  static final int OK = 0, DENIED = 1, UNAVAILABLE = 2, BUSY = 3, INVALID = 4, FAILED = 5;
  static final int MAX_SECONDS = 30;

  interface Device {
    void set(int level) throws Exception;
  }

  private final Device device;
  private final BooleanSupplier allowed;
  private final LongSupplier clock;
  private final int maximum, defaultLevel;
  private boolean available, observedOn, owned, closed;
  private long deadline;
  private int level;

  VmTorchLease(
      Device device, BooleanSupplier allowed, LongSupplier clock, int maximum, int defaultLevel) {
    if (maximum < 1 || maximum > 255 || defaultLevel < 1 || defaultLevel > maximum)
      throw new IllegalArgumentException("Invalid flash capability");
    this.device = device;
    this.allowed = allowed;
    this.clock = clock;
    this.maximum = maximum;
    this.defaultLevel = defaultLevel;
  }

  synchronized void observe(boolean available, boolean enabled) {
    this.available = available;
    observedOn = enabled;
    if (!enabled || !available) {
      owned = false;
      deadline = 0;
      level = 0;
    }
  }

  synchronized byte[] request(byte[] request) {
    if (closed || !allowed.getAsBoolean()) {
      boolean stopped = release();
      return response(stopped ? DENIED : FAILED);
    }
    if (!expire()) return response(FAILED);
    if (request.length != REQUEST_BYTES || (request[0] & 255) != VERSION) return response(INVALID);
    int operation = request[1] & 255, requestedLevel = request[2] & 255, seconds = request[3] & 255;
    if (operation > 2
        || (operation != 1 && (requestedLevel != 0 || seconds != 0))
        || (operation == 1 && (seconds < 1 || seconds > MAX_SECONDS || requestedLevel > maximum)))
      return response(INVALID);
    if (operation == 0) return response(OK);
    if (operation == 2) return response(release() ? OK : FAILED);
    if (!available) return response(UNAVAILABLE);
    if (observedOn && !owned) return response(BUSY);
    int nextLevel = requestedLevel == 0 ? defaultLevel : requestedLevel;
    try {
      device.set(nextLevel);
      owned = observedOn = true;
      level = nextLevel;
      deadline = clock.getAsLong() + seconds * 1000L;
      return response(OK);
    } catch (Exception failed) {
      return response(FAILED);
    }
  }

  synchronized long remainingMillis() {
    return owned ? Math.max(0, deadline - clock.getAsLong()) : 0;
  }

  synchronized boolean expire() {
    return !owned || (allowed.getAsBoolean() && clock.getAsLong() < deadline) || release();
  }

  private boolean release() {
    if (!owned) return true;
    try {
      device.set(0);
      owned = observedOn = false;
      deadline = 0;
      level = 0;
      return true;
    } catch (Exception failed) {
      // Do not claim the lamp is off if the camera service rejected the request.
      return false;
    }
  }

  private byte[] response(int status) {
    int seconds = (int) Math.min(MAX_SECONDS, (remainingMillis() + 999) / 1000);
    return new byte[] {
      VERSION,
      (byte) status,
      (byte) (observedOn ? 1 : 0),
      (byte) (available ? 1 : 0),
      (byte) maximum,
      (byte) level,
      (byte) seconds,
      (byte) (owned ? 1 : 0)
    };
  }

  @Override
  public synchronized void close() {
    closed = true;
    if (!release()) throw new IllegalStateException("LED off was not confirmed");
  }
}
