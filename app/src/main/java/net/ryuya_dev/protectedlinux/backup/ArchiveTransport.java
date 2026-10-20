package net.ryuya_dev.protectedlinux.backup;

import java.io.*;

/** Framed data ends without closing stdin, leaving a separate explicit commit byte. */
public final class ArchiveTransport {
  public static final int COMMIT = 0x5a;

  private ArchiveTransport() {}

  public static void send(InputStream input, OutputStream sink) throws IOException {
    DataOutputStream output = new DataOutputStream(sink);
    byte[] bytes = new byte[65536];
    int count;
    while ((count = input.read(bytes)) != -1) {
      if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
      if (count == 0) continue;
      output.writeInt(count);
      output.write(bytes, 0, count);
    }
    output.writeInt(0);
    output.flush();
  }

  public static InputStream receive(InputStream source) {
    return new InputStream() {
      private final DataInputStream input = new DataInputStream(source);
      private int remaining;
      private boolean ended;

      @Override
      public int read() throws IOException {
        byte[] one = new byte[1];
        return read(one, 0, 1) < 0 ? -1 : one[0] & 255;
      }

      @Override
      public int read(byte[] bytes, int offset, int length) throws IOException {
        java.util.Objects.checkFromIndexSize(offset, length, bytes.length);
        if (length == 0) return 0;
        if (ended) return -1;
        if (remaining == 0) {
          remaining = input.readInt();
          if (remaining < 0 || remaining > 65536) throw new IOException("ARCHIVE_FRAME");
          if (remaining == 0) {
            ended = true;
            return -1;
          }
        }
        int count = input.read(bytes, offset, Math.min(length, remaining));
        if (count < 0) throw new EOFException("ARCHIVE_FRAME");
        remaining -= count;
        return count;
      }

      @Override
      public void close() {
        /* Caller owns stdin and its later commit byte. */
      }
    };
  }

  public static void requireCommit(InputStream input) throws IOException {
    if (input.read() != COMMIT) throw new IOException("ARCHIVE_CANCELLED");
  }
}
