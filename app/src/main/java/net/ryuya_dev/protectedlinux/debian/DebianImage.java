package net.ryuya_dev.protectedlinux.debian;

import java.io.*;
import java.nio.*;
import java.nio.channels.FileChannel;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.*;
import java.util.*;
import java.util.function.LongConsumer;
import java.util.zip.GZIPInputStream;

/** Pinned ARM64 image installation. Never executes archive files or extracts supplied paths. */
public final class DebianImage {
  public static final String URL =
      "https://dl.google.com/android/ferrochrome/5100000/aarch64/images.tar.gz";
  public static final long ARCHIVE_BYTES = 607391791L;
  public static final String ARCHIVE_SHA256 =
      "443400a1d190df478592c5822ee9f02ff5f9684b6453c55ec6fff47156c8aab5";
  public static final long ROOT_BYTES = 3085959168L;
  public static final String ROOT_SHA256 =
      "1db9be276e3e433c4da31a885ac3f67af4747098850dfad8ca9e43023283cc67";
  private static final Set<String> ENTRIES =
      Set.of("build_id", "root_part", "vm_config.json", "vmlinuz", "initrd.img");

  private DebianImage() {}

  private static MessageDigest digest() {
    try {
      return MessageDigest.getInstance("SHA-256");
    } catch (NoSuchAlgorithmException impossible) {
      throw new AssertionError(impossible);
    }
  }

  public static void install(InputStream source, Path destination, LongConsumer progress)
      throws IOException {
    MessageDigest archive = digest();
    CountingInput input = new CountingInput(new DigestInputStream(source, archive), progress);
    try (GZIPInputStream gzip = new GZIPInputStream(input, 65536)) {
      unpack(gzip, destination);
      // Read through gzip's CRC/trailer, including normal TAR record padding.
      byte[] buffer = new byte[65536];
      long padding = 0;
      int count;
      while ((count = gzip.read(buffer)) != -1) {
        padding += count;
        if (padding > 10240 || !zero(buffer, count)) throw new IOException("ARCHIVE_IMAGE_FORMAT");
      }
      while (input.read(buffer) != -1) {
        /* Count/hash any trailing compressed bytes too. */
      }
      if (input.count != ARCHIVE_BYTES
          || !HexFormat.of().formatHex(archive.digest()).equals(ARCHIVE_SHA256))
        throw new IOException("ARCHIVE_IMAGE_CHECKSUM");
    }
    verifyRoot(destination);
  }

  /** Also used for importing an already installed stock Terminal image. */
  public static void importRaw(InputStream input, Path destination, LongConsumer progress)
      throws IOException {
    writeSparse(input, ROOT_BYTES, destination, progress);
    if (input.read() != -1) throw new IOException("ARCHIVE_IMAGE_SIZE");
    verifyRoot(destination);
  }

  // Intentionally supports only the observed, pinned USTAR regular-file archive.
  // An upstream packaging change must be reviewed instead of enabling broad extraction.
  static void unpack(InputStream input, Path destination) throws IOException {
    Set<String> names = new HashSet<>();
    for (int entry = 0; entry <= ENTRIES.size(); entry++) {
      byte[] header = exact(input, 512);
      if (zero(header, header.length)) {
        if (!zero(exact(input, 512), 512) || !names.equals(ENTRIES))
          throw new IOException("ARCHIVE_IMAGE_FORMAT");
        return;
      }
      long expected = octal(header, 148, 8), checksum = 0;
      for (int i = 0; i < 512; i++) checksum += i >= 148 && i < 156 ? 32 : header[i] & 255;
      String name = text(header, 0, 100);
      if (checksum != expected
          || header[156] != 0 && header[156] != '0'
          || !text(header, 345, 155).isEmpty()
          || !ENTRIES.contains(name)
          || !names.add(name)) throw new IOException("ARCHIVE_IMAGE_FORMAT");
      long size = octal(header, 124, 12);
      if (size < 0
          || size > (name.equals("root_part") ? ROOT_BYTES : 64L * 1024 * 1024)
          || name.equals("root_part") && size != ROOT_BYTES)
        throw new IOException("ARCHIVE_IMAGE_SIZE");
      if (name.equals("root_part")) writeSparse(input, size, destination, bytes -> {});
      else consume(input, size);
      consume(input, (512 - size % 512) % 512);
    }
    throw new IOException("ARCHIVE_IMAGE_FORMAT");
  }

  private static byte[] exact(InputStream input, int length) throws IOException {
    byte[] bytes = input.readNBytes(length);
    if (bytes.length != length) throw new EOFException("ARCHIVE_IMAGE_TRUNCATED");
    return bytes;
  }

  private static void consume(InputStream input, long remaining) throws IOException {
    byte[] bytes = new byte[65536];
    while (remaining > 0) {
      if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
      int count = input.read(bytes, 0, (int) Math.min(bytes.length, remaining));
      if (count < 0) throw new EOFException("ARCHIVE_IMAGE_TRUNCATED");
      remaining -= count;
    }
  }

  private static String text(byte[] bytes, int offset, int length) throws IOException {
    int end = offset;
    while (end < offset + length && bytes[end] != 0) {
      if (bytes[end] < 32 || bytes[end] > 126) throw new IOException("ARCHIVE_IMAGE_FORMAT");
      end++;
    }
    return new String(bytes, offset, end - offset, java.nio.charset.StandardCharsets.US_ASCII);
  }

  private static long octal(byte[] header, int offset, int length) throws IOException {
    String value = text(header, offset, length).trim();
    if (!value.matches("[0-7]{1,12}")) throw new IOException("ARCHIVE_IMAGE_FORMAT");
    try {
      return Long.parseLong(value, 8);
    } catch (NumberFormatException invalid) {
      throw new IOException("ARCHIVE_IMAGE_FORMAT", invalid);
    }
  }

  private static boolean zero(byte[] data, int length) {
    for (int i = 0; i < length; i++) if (data[i] != 0) return false;
    return true;
  }

  private static void writeSparse(
      InputStream input, long size, Path destination, LongConsumer progress) throws IOException {
    // Direct statvfs avoids mount-table enumeration in Android root workers.
    if (destination.getParent().toFile().getUsableSpace() < ROOT_BYTES + 512L * 1024 * 1024)
      throw new IOException("ARCHIVE_SPACE");
    try (FileChannel output =
        FileChannel.open(
            destination,
            StandardOpenOption.CREATE_NEW,
            StandardOpenOption.WRITE,
            LinkOption.NOFOLLOW_LINKS)) {
      Files.setPosixFilePermissions(destination, PosixFilePermissions.fromString("rw-------"));
      byte[] buffer = new byte[65536];
      long done = 0;
      while (done < size) {
        if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
        int count = input.read(buffer, 0, (int) Math.min(buffer.length, size - done));
        if (count < 0) throw new EOFException("ARCHIVE_IMAGE_TRUNCATED");
        if (zero(buffer, count)) output.position(output.position() + count);
        else {
          ByteBuffer bytes = ByteBuffer.wrap(buffer, 0, count);
          while (bytes.hasRemaining()) output.write(bytes);
        }
        done += count;
        progress.accept(done);
      }
      if (output.size() < size) {
        output.position(size - 1);
        output.write(ByteBuffer.wrap(new byte[1]));
      }
      output.force(true);
    }
  }

  private static void verifyRoot(Path destination) throws IOException {
    MessageDigest hash = digest();
    try (InputStream input = Files.newInputStream(destination, LinkOption.NOFOLLOW_LINKS)) {
      byte[] buffer = new byte[65536];
      int count;
      while ((count = input.read(buffer)) != -1) {
        if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
        hash.update(buffer, 0, count);
      }
    }
    if (Files.size(destination) != ROOT_BYTES
        || !HexFormat.of().formatHex(hash.digest()).equals(ROOT_SHA256))
      throw new IOException("ARCHIVE_IMAGE_CHECKSUM");
    Files.setPosixFilePermissions(destination, PosixFilePermissions.fromString("r--------"));
    try (FileChannel channel = FileChannel.open(destination, StandardOpenOption.READ)) {
      channel.force(true);
    }
  }

  private static final class CountingInput extends FilterInputStream {
    private long count;
    private final LongConsumer progress;

    CountingInput(InputStream input, LongConsumer progress) {
      super(input);
      this.progress = progress;
    }

    @Override
    public int read() throws IOException {
      int value = in.read();
      if (value >= 0) record(1);
      return value;
    }

    @Override
    public int read(byte[] bytes, int offset, int length) throws IOException {
      int result = in.read(bytes, offset, length);
      if (result > 0) record(result);
      return result;
    }

    private void record(int size) throws IOException {
      count += size;
      if (count > ARCHIVE_BYTES) throw new IOException("ARCHIVE_IMAGE_SIZE");
      progress.accept(count);
    }
  }
}
