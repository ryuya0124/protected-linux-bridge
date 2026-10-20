package net.ryuya_dev.protectedlinux.debian;

import static org.junit.Assert.*;

import java.io.*;
import java.nio.file.*;
import java.util.*;
import org.junit.*;
import org.junit.rules.TemporaryFolder;

public class DebianImageTest {
  @Rule public TemporaryFolder temporary = new TemporaryFolder();

  @Test
  public void tarTraversalLinksDuplicateAndUnexpectedEntriesAreRejected() throws Exception {
    for (String name :
        List.of("../root_part", "/root_part", "./root_part", "unknown", "root_part/")) {
      byte[] header = header(name, 0, '0');
      assertThrows(
          IOException.class, () -> DebianImage.unpack(new ByteArrayInputStream(header), target()));
    }
    for (char type : new char[] {'1', '2', '3', '5', 'x', 'L'}) {
      assertThrows(
          IOException.class,
          () ->
              DebianImage.unpack(new ByteArrayInputStream(header("root_part", 0, type)), target()));
    }
    ByteArrayOutputStream duplicate = new ByteArrayOutputStream();
    duplicate.write(header("build_id", 0, '0'));
    duplicate.write(header("build_id", 0, '0'));
    assertThrows(
        IOException.class,
        () -> DebianImage.unpack(new ByteArrayInputStream(duplicate.toByteArray()), target()));
    assertFalse(Files.exists(temporary.getRoot().toPath().resolve("root_part")));
  }

  @Test
  public void truncatedAndDamagedHeadersAndShortRootRefused() throws Exception {
    assertThrows(
        IOException.class,
        () -> DebianImage.unpack(new ByteArrayInputStream(new byte[511]), target()));
    byte[] corrupt = header("root_part", DebianImage.ROOT_BYTES, '0');
    corrupt[1] ^= 1;
    assertThrows(
        IOException.class, () -> DebianImage.unpack(new ByteArrayInputStream(corrupt), target()));
    assertThrows(
        IOException.class,
        () -> DebianImage.unpack(new ByteArrayInputStream(header("root_part", 1, '0')), target()));
    assertThrows(
        IOException.class,
        () -> DebianImage.unpack(new ByteArrayInputStream(new byte[1024]), target()));
  }

  @Test
  public void badGzipAndTruncatedRawCannotCreateACompletedImage() throws Exception {
    assertThrows(
        IOException.class,
        () ->
            DebianImage.install(
                new ByteArrayInputStream(new byte[] {1, 2, 3}), target(), bytes -> {}));
    assertThrows(
        IOException.class,
        () -> DebianImage.importRaw(new ByteArrayInputStream(new byte[16]), target(), bytes -> {}));
  }

  private Path target() {
    return temporary.getRoot().toPath().resolve(UUID.randomUUID().toString());
  }

  private byte[] header(String name, long length, char type) {
    byte[] data = new byte[512];
    byte[] nameBytes = name.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    System.arraycopy(nameBytes, 0, data, 0, nameBytes.length);
    byte[] size =
        String.format(Locale.ROOT, "%011o", length)
            .getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    System.arraycopy(size, 0, data, 124, size.length);
    data[156] = (byte) type;
    Arrays.fill(data, 148, 156, (byte) ' ');
    int checksum = 0;
    for (byte value : data) checksum += value & 255;
    byte[] sum =
        String.format(Locale.ROOT, "%06o", checksum)
            .getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    System.arraycopy(sum, 0, data, 148, sum.length);
    data[154] = 0;
    data[155] = ' ';
    return data;
  }
}
