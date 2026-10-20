package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import java.io.*;
import java.util.*;
import net.ryuya_dev.protectedlinux.display.*;
import org.junit.Test;

public class OwnedDisplayTest {
  private final DisplayCapability capability = new DisplayCapability(10123, "a".repeat(64));

  @Test
  public void capabilityHasFixedBinaryFraming() throws Exception {
    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
    capability.write(bytes);
    assertEquals(40, bytes.size());
    DisplayCapability restored =
        DisplayCapability.read(new ByteArrayInputStream(bytes.toByteArray()));
    assertEquals(10123, restored.viewerUid);
    assertEquals(capability.session, restored.session);
    for (int length = 0; length < 40; length++) {
      byte[] truncated = Arrays.copyOf(bytes.toByteArray(), length);
      assertThrows(
          IOException.class, () -> DisplayCapability.read(new ByteArrayInputStream(truncated)));
    }
    byte[] corrupt = bytes.toByteArray();
    corrupt[0] = 0;
    assertThrows(
        IOException.class, () -> DisplayCapability.read(new ByteArrayInputStream(corrupt)));
    assertThrows(
        IllegalArgumentException.class, () -> new DisplayCapability(0, capability.session));
    assertThrows(
        IllegalArgumentException.class, () -> new DisplayCapability(10123, "A".repeat(64)));
  }

  @Test
  public void onlyClaimingRootProcessCanPublishOrClose() {
    DisplayLease lease = new DisplayLease(capability);
    Object publisher = new Object();
    assertFalse(lease.mayPublish(0, 42, capability.session));
    assertFalse(lease.claim(2000, 42, capability.session));
    assertFalse(lease.claim(0, 1, capability.session));
    assertFalse(lease.claim(0, 42, "b".repeat(64)));
    assertTrue(lease.claim(0, 42, capability.session));
    assertFalse(lease.claim(0, 43, capability.session));
    assertFalse(lease.publish(0, 43, capability.session, publisher));
    assertTrue(lease.publish(0, 42, capability.session, publisher));
    assertFalse(lease.publish(0, 42, capability.session, new Object()));
    assertFalse(lease.died(new Object()));
    assertTrue(lease.open());
    assertFalse(lease.revoke(0, 43, capability.session));
    assertTrue(lease.died(publisher));
    assertFalse(lease.open());
    assertFalse(lease.claim(0, 42, capability.session));
  }

  @Test
  public void revokeAndViewerCloseAreTerminal() {
    DisplayLease lease = new DisplayLease(capability);
    assertTrue(lease.claim(0, 42, capability.session));
    assertTrue(lease.revoke(0, 42, capability.session));
    assertFalse(lease.publish(0, 42, capability.session, new Object()));
    lease.close();
    assertFalse(lease.claim(0, 42, capability.session));
  }

  @Test
  public void staleDetachCannotRemoveNewSurfaceAndLateAttachIsReleased() {
    List<String> released = new ArrayList<>();
    DisplayTimeline<String> timeline = new DisplayTimeline<>(released::add);
    assertTrue(timeline.attach(1, "old", false));
    assertTrue(timeline.attach(3, "new", false));
    assertEquals(List.of("old"), released);
    assertFalse(timeline.detach(1, false));
    assertEquals("new", timeline.frame(false));
    assertTrue(timeline.detach(4, false));
    assertFalse(timeline.attach(4, "late", false));
    assertNull(timeline.frame(false));
    assertEquals(List.of("old", "new", "late"), released);
    assertTrue(timeline.attach(5, "main", false));
    assertTrue(timeline.attach(1, "cursor", true));
    timeline.close();
    timeline.close();
    assertFalse(timeline.attach(6, "after-close", false));
    assertFalse(timeline.detach(6, false));
    assertEquals(List.of("old", "new", "late", "main", "cursor", "after-close"), released);
  }
}
