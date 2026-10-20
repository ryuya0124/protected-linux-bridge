package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import org.junit.Test;

public class HostContractTest {
  @Test
  public void onlyInspectedAndroid17IdentityMatches() {
    assertTrue(HostContract.matches(37, "SM-S948Q", "S948QOPU1BZI9", HostContract.KERNEL, 37));
    assertFalse(HostContract.matches(36, "SM-S948Q", "S948QOPU1BZI9", HostContract.KERNEL, 37));
    assertFalse(HostContract.matches(37, "SM-S948Q", "S948QOPU1BZI9", HostContract.KERNEL, 36));
    assertFalse(HostContract.matches(37, "SM-S948Q", "next", HostContract.KERNEL, 37));
    assertFalse(HostContract.matches(37, "SM-S948Q", "S948QOPU1BZI9", "different", 37));
    assertFalse(HostContract.matches(37, "SM-S942Q", "S948QOPU1BZI9", HostContract.KERNEL, 37));
    assertFalse(HostContract.matches(38, null, null, null, 38));
  }

  @Test
  public void missingHostRendererCannotEnableAndroidGuest() {
    assertFalse(HostContract.ANDROID_GUEST_READY);
  }
}
