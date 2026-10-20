package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import java.util.ArrayList;
import org.junit.Test;

public class VmTorchLeaseTest {
  private long now;
  private boolean allowed = true, failOff;
  private final ArrayList<Integer> levels = new ArrayList<>();

  private VmTorchLease lease() {
    return new VmTorchLease(
        level -> {
          if (failOff && level == 0) throw new Exception("camera busy");
          levels.add(level);
        },
        () -> allowed,
        () -> now,
        5,
        2);
  }

  private byte[] packet(int operation, int level, int seconds) {
    return new byte[] {1, (byte) operation, (byte) level, (byte) seconds};
  }

  private int status(byte[] result) {
    return result[1] & 255;
  }

  @Test
  public void requiresConsentAndReportedAvailability() {
    var lease = lease();
    assertEquals(VmTorchLease.UNAVAILABLE, status(lease.request(packet(1, 0, 10))));
    lease.observe(true, false);
    allowed = false;
    assertEquals(VmTorchLease.DENIED, status(lease.request(packet(1, 0, 10))));
    assertTrue(levels.isEmpty());
  }

  @Test
  public void refusesMalformedAndUnboundedRequests() {
    var lease = lease();
    lease.observe(true, false);
    for (byte[] invalid :
        new byte[][] {
          new byte[0],
          new byte[5],
          {2, 1, 1, 1},
          packet(3, 0, 0),
          packet(1, 6, 1),
          packet(1, 0, 0),
          packet(1, 0, 31),
          packet(0, 1, 0),
          packet(2, 0, 1)
        }) assertEquals(VmTorchLease.INVALID, status(lease.request(invalid)));
    assertTrue(levels.isEmpty());
  }

  @Test
  public void neverTakesOverOrSwitchesOffAnExistingLamp() {
    var lease = lease();
    lease.observe(true, true);
    assertEquals(VmTorchLease.BUSY, status(lease.request(packet(1, 1, 10))));
    assertEquals(VmTorchLease.OK, status(lease.request(packet(2, 0, 0))));
    lease.close();
    assertTrue(levels.isEmpty());
  }

  @Test
  public void expiresRenewsAndReleasesOnRevocation() {
    var lease = lease();
    lease.observe(true, false);
    assertEquals(VmTorchLease.OK, status(lease.request(packet(1, 0, 10))));
    assertEquals(2, (int) levels.get(0));
    now = 9000;
    lease.request(packet(1, 4, 20));
    now = 10000;
    assertTrue(lease.expire());
    assertEquals(2, levels.size());
    assertEquals(19000, lease.remainingMillis());
    allowed = false;
    assertTrue(lease.expire());
    assertEquals(0, (int) levels.get(2));
    lease.close();
    assertEquals(3, levels.size());
  }

  @Test
  public void deadlineStopsOnceAndClosedSessionCannotRelight() {
    var lease = lease();
    lease.observe(true, false);
    lease.request(packet(1, 1, 1));
    now = 1000;
    assertTrue(lease.expire());
    assertTrue(lease.expire());
    lease.close();
    assertEquals(VmTorchLease.DENIED, status(lease.request(packet(1, 1, 1))));
    assertEquals(java.util.List.of(1, 0), levels);
  }

  @Test
  public void offFailureIsReportedRatherThanClaimingSuccess() {
    var lease = lease();
    lease.observe(true, false);
    lease.request(packet(1, 1, 1));
    failOff = true;
    now = 1000;
    assertFalse(lease.expire());
    assertEquals(VmTorchLease.FAILED, status(lease.request(packet(0, 0, 0))));
    assertThrows(IllegalStateException.class, lease::close);
    failOff = false;
    lease.close();
    assertEquals(java.util.List.of(1, 0), levels);
  }
}
