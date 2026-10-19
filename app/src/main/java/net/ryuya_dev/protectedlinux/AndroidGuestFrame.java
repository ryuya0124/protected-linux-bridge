package net.ryuya_dev.protectedlinux;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Accept only the pinned raw screencap format; never interpret unchecked pixels. */
final class AndroidGuestFrame {
  static final int SIZE = 16 + 480 * 800 * 4;

  static ByteBuffer pixels(byte[] frame, int size) throws IOException {
    if (size != SIZE || frame.length < size) throw new IOException("raw frame length");
    ByteBuffer bytes = ByteBuffer.wrap(frame, 0, size).order(ByteOrder.LITTLE_ENDIAN);
    if (bytes.getInt() != 480
        || bytes.getInt() != 800
        || bytes.getInt() != 1
        || bytes.getInt() != 1) {
      throw new IOException("raw frame format");
    }
    return bytes.slice();
  }
}
