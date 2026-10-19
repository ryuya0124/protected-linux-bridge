package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.Test;

public final class AndroidGuestFrameTest {
  private byte[] frame() {
    byte[] bytes = new byte[AndroidGuestFrame.SIZE];
    ByteBuffer.wrap(bytes)
        .order(ByteOrder.LITTLE_ENDIAN)
        .putInt(480)
        .putInt(800)
        .putInt(1)
        .putInt(1);
    return bytes;
  }

  @Test
  public void headerIsExcludedAndRgbaBytesAreNotReordered() throws Exception {
    byte[] frame = frame();
    frame[16] = (byte) 0xff;
    frame[17] = 23;
    frame[18] = 47;
    frame[19] = (byte) 0xff;
    ByteBuffer pixels = AndroidGuestFrame.pixels(frame, frame.length);
    assertEquals(480 * 800 * 4, pixels.remaining());
    assertEquals(255, pixels.get() & 255);
    assertEquals(23, pixels.get() & 255);
    assertEquals(47, pixels.get() & 255);
    assertEquals(255, pixels.get() & 255);
  }

  @Test
  public void malformedDimensionsFormatsAndLengthsFailClosed() {
    byte[] frame = frame();
    assertThrows(IOException.class, () -> AndroidGuestFrame.pixels(frame, frame.length - 1));
    assertThrows(IOException.class, () -> AndroidGuestFrame.pixels(new byte[16], frame.length));
    for (int field = 0; field < 4; field++) {
      byte[] invalid = frame();
      ByteBuffer.wrap(invalid).order(ByteOrder.LITTLE_ENDIAN).putInt(field * 4, 0);
      assertThrows(IOException.class, () -> AndroidGuestFrame.pixels(invalid, invalid.length));
    }
  }
}
