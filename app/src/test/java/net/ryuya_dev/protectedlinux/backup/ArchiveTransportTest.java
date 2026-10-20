package net.ryuya_dev.protectedlinux.backup;

import static org.junit.Assert.*;

import java.io.*;
import java.util.*;
import org.junit.Test;

public class ArchiveTransportTest {
  @Test
  public void framedEofDoesNotConsumeCommitOrCloseUnderlyingPipe() throws Exception {
    byte[] data = new byte[200000];
    new Random(7).nextBytes(data);
    ByteArrayOutputStream output = new ByteArrayOutputStream();
    ArchiveTransport.send(new ByteArrayInputStream(data), output);
    output.write(ArchiveTransport.COMMIT);
    ByteArrayInputStream input = new ByteArrayInputStream(output.toByteArray());
    try (InputStream frames = ArchiveTransport.receive(input)) {
      assertArrayEquals(data, frames.readAllBytes());
      assertEquals(-1, frames.read());
    }
    ArchiveTransport.requireCommit(input);
    assertEquals(-1, input.read());
  }

  @Test
  public void brokenFramesAndMissingConsentRefuseCommit() throws Exception {
    for (byte[] bytes :
        List.of(
            new byte[0],
            new byte[] {0, 0, 0, 5, 1},
            new byte[] {0, 1, 0, 1},
            new byte[] {-1, -1, -1, -1}))
      assertThrows(
          IOException.class,
          () -> ArchiveTransport.receive(new ByteArrayInputStream(bytes)).readAllBytes());
    for (byte[] value : List.of(new byte[0], new byte[] {0}, new byte[] {1}))
      assertThrows(
          IOException.class, () -> ArchiveTransport.requireCommit(new ByteArrayInputStream(value)));
  }
}
