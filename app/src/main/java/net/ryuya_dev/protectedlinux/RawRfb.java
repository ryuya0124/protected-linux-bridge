package net.ryuya_dev.protectedlinux;

import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** RFB 3.8 raw rectangles only: no JPEG/zlib/JavaScript image decode. */
final class RawRfb {
  interface Wire {
    int read(ByteBuffer data) throws IOException;

    void send(byte[] data) throws IOException;
  }

  interface Sink {
    void size(int width, int height) throws IOException;

    void frame(ByteBuffer pixels, List<Rect> damage) throws IOException;

    default void desktopResizeAvailable() {}

    default void desktopResizeRejected() {}
  }

  record Rect(int x, int y, int width, int height) {}

  private final Wire wire;
  private final Sink sink;
  private volatile boolean desktopResize;
  private volatile int screenId, screenFlags;
  private volatile int width, height;
  private ByteBuffer pixels;

  RawRfb(Wire wire, Sink sink) {
    this.wire = wire;
    this.sink = sink;
  }

  private void exact(ByteBuffer into) throws IOException {
    while (into.hasRemaining()) {
      int n = wire.read(into);
      if (n <= 0) throw new EOFException("Display disconnected");
    }
  }

  private byte[] bytes(int count) throws IOException {
    byte[] result = new byte[count];
    exact(ByteBuffer.wrap(result));
    return result;
  }

  private int u8() throws IOException {
    return bytes(1)[0] & 255;
  }

  private int u16() throws IOException {
    return ByteBuffer.wrap(bytes(2)).getShort() & 65535;
  }

  private int i32() throws IOException {
    return ByteBuffer.wrap(bytes(4)).getInt();
  }

  static int imageBytes(int w, int h) throws IOException {
    if (w < 1 || h < 1 || w > 4096 || h > 4096 || (long) w * h > 4_194_304)
      throw new IOException("Unsupported framebuffer size");
    return w * h * 4;
  }

  private void resize(int w, int h) throws IOException {
    int length = imageBytes(w, h);
    width = w;
    height = h;
    pixels = ByteBuffer.allocateDirect(length);
    sink.size(w, h);
  }

  void handshake() throws IOException {
    String version = new String(bytes(12), StandardCharsets.US_ASCII);
    if (!version.equals("RFB 003.008\n")) throw new IOException("Unsupported display protocol");
    wire.send("RFB 003.008\n".getBytes(StandardCharsets.US_ASCII));
    int count = u8();
    if (count < 1 || count > 32) throw new IOException("Display authentication unavailable");
    boolean none = false;
    for (byte type : bytes(count)) if (type == 1) none = true;
    if (!none) throw new IOException("Display authentication unsupported");
    wire.send(new byte[] {1});
    if (i32() != 0) throw new IOException("Display authentication failed");
    wire.send(new byte[] {1});
    int w = u16(), h = u16();
    imageBytes(w, h);
    bytes(16);
    int title = i32();
    if (title < 0 || title > 4096) throw new IOException("Invalid display name length");
    bytes(title);
    resize(w, h);
    // 32-bit little-endian RGBX, byte order R/G/B/X. Shader supplies opaque alpha.
    wire.send(
        new byte[] {
          0, 0, 0, 0, 32, 24, 0, 1, 0, (byte) 255, 0, (byte) 255, 0, (byte) 255, 0, 8, 16, 0, 0, 0
        });
    ByteBuffer enc = ByteBuffer.allocate(16);
    enc.put((byte) 2).put((byte) 0).putShort((short) 3).putInt(0).putInt(-223).putInt(-308);
    wire.send(enc.array());
    request(false);
  }

  boolean resizeDesktop(int w, int h) throws IOException {
    imageBytes(w, h);
    if (!desktopResize) return false;
    if (w == width && h == height) return true;
    ByteBuffer b = ByteBuffer.allocate(24);
    b.put((byte) 251)
        .put((byte) 0)
        .putShort((short) w)
        .putShort((short) h)
        .put((byte) 1)
        .put((byte) 0)
        .putInt(screenId)
        .putShort((short) 0)
        .putShort((short) 0)
        .putShort((short) w)
        .putShort((short) h)
        .putInt(screenFlags);
    wire.send(b.array());
    return true;
  }

  void request(boolean incremental) throws IOException {
    ByteBuffer b = ByteBuffer.allocate(10);
    b.put((byte) 3)
        .put((byte) (incremental ? 1 : 0))
        .putShort((short) 0)
        .putShort((short) 0)
        .putShort((short) width)
        .putShort((short) height);
    wire.send(b.array());
  }

  void update() throws IOException {
    int type = u8();
    if (type == 2) return; // bell has no body
    if (type == 3) {
      bytes(3);
      int length = i32();
      if (length < 0 || length > 65536) throw new IOException("Clipboard message too large");
      while (length > 0) {
        int n = Math.min(length, 4096);
        bytes(n);
        length -= n;
      }
      return;
    }
    if (type != 0) throw new IOException("Unknown display message");
    u8();
    int count = u16();
    if (count > 4096) throw new IOException("Too many display rectangles");
    boolean resized = false;
    List<Rect> damage = new ArrayList<>();
    long budget = (long) width * height * 8;
    for (int i = 0; i < count; i++) {
      int x = u16(), y = u16(), w = u16(), h = u16(), encoding = i32();
      if (encoding == -308) {
        if (x > 2 || y > 3) throw new IOException("Invalid desktop resize result");
        int screens = u8();
        bytes(3);
        if (screens > 16) throw new IOException("Invalid screen count");
        int id = 0, flags = 0;
        for (int j = 0; j < screens; j++) {
          int nextId = i32(), sx = u16(), sy = u16(), sw = u16(), sh = u16(), nextFlags = i32();
          if (!(x == 1 && y != 0) && (sw < 1 || sh < 1 || sx > w - sw || sy > h - sh))
            throw new IOException("Invalid screen layout");
          if (j == 0) {
            id = nextId;
            flags = nextFlags;
          }
        }
        if (x == 1 && y != 0) {
          sink.desktopResizeRejected();
          continue;
        }
        if (screens < 1) throw new IOException("Missing screen layout");
        if (w != width || h != height) {
          if (!damage.isEmpty()) throw new IOException("Resize follows pixel data");
          resize(w, h);
          budget = (long) width * height * 8;
          resized = true;
        }
        if (screens == 1) {
          boolean first = !desktopResize;
          screenId = id;
          screenFlags = flags;
          desktopResize = true;
          if (first) sink.desktopResizeAvailable();
        } else desktopResize = false;
        continue;
      }
      if (encoding == -223) {
        if (x != 0 || y != 0 || i != 0) throw new IOException("Invalid resize rectangle");
        resize(w, h);
        budget = (long) width * height * 8;
        damage.clear();
        resized = true;
        continue;
      }
      if (encoding != 0 || w < 1 || h < 1 || x > width - w || y > height - h)
        throw new IOException("Invalid raw rectangle");
      budget -= (long) w * h * 4;
      if (budget < 0) throw new IOException("Display update too large");
      if (x == 0 && w == width) {
        ByteBuffer target = pixels.duplicate();
        target.position(y * width * 4);
        target.limit(target.position() + w * h * 4);
        exact(target);
      } else
        for (int row = 0; row < h; row++) {
          ByteBuffer target = pixels.duplicate();
          target.position(((y + row) * width + x) * 4);
          target.limit(target.position() + w * 4);
          exact(target);
        }
      damage.add(new Rect(x, y, w, h));
    }
    if (!damage.isEmpty()) sink.frame(pixels, List.copyOf(damage));
    request(!resized);
  }

  void pointer(int x, int y, int buttons) throws IOException {
    if (width < 1 || height < 1) return;
    ByteBuffer b = ByteBuffer.allocate(6);
    b.put((byte) 5)
        .put((byte) buttons)
        .putShort((short) Math.max(0, Math.min(width - 1, x)))
        .putShort((short) Math.max(0, Math.min(height - 1, y)));
    wire.send(b.array());
  }

  void key(int keysym, boolean down) throws IOException {
    ByteBuffer b = ByteBuffer.allocate(8);
    b.put((byte) 4).put((byte) (down ? 1 : 0)).putShort((short) 0).putInt(keysym);
    wire.send(b.array());
  }
}
