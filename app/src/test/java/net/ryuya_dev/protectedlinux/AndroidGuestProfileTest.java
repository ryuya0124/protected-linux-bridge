package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import org.junit.Test;

/** The UI, native artifact and acknowledgement must select the same bounded preset. */
public final class AndroidGuestProfileTest {
  @Test
  public void selectorMapsToDistinctRuntimeArtifacts() {
    assertEquals(1536, AndroidGuestProfile.memoryAt(0));
    assertEquals(1792, AndroidGuestProfile.memoryAt(1));
    assertEquals(512, AndroidGuestProfile.memoryAt(2));
    assertEquals(4096, AndroidGuestProfile.memoryAt(3));
    assertEquals("android-gpu4g-bridge", AndroidGuestProfile.executable(4096));
    assertTrue(AndroidGuestProfile.guarded(4096));
    assertEquals("android-bridge", AndroidGuestProfile.executable(512));
    assertEquals("android-large-bridge", AndroidGuestProfile.executable(1536));
    assertEquals("android-large1792-bridge", AndroidGuestProfile.executable(1792));
    assertNotEquals(AndroidGuestProfile.library(1536), AndroidGuestProfile.library(1792));
    assertTrue(AndroidGuestProfile.guarded(1536));
    assertTrue(AndroidGuestProfile.guarded(1792));
    assertFalse(AndroidGuestProfile.guarded(512));
  }

  @Test
  public void unknownPresetsCannotSelectAFallbackArtifact() {
    for (int memory : new int[] {0, -1, 1024, 2048, 6144}) {
      assertFalse(AndroidGuestProfile.supported(memory));
      assertThrows(IllegalArgumentException.class, () -> AndroidGuestProfile.executable(memory));
      assertThrows(IllegalArgumentException.class, () -> AndroidGuestProfile.library(memory));
    }
    assertThrows(IllegalArgumentException.class, () -> AndroidGuestProfile.library(512));
    assertThrows(IllegalArgumentException.class, () -> AndroidGuestProfile.memoryAt(-1));
    assertThrows(IllegalArgumentException.class, () -> AndroidGuestProfile.memoryAt(4));
  }
}
