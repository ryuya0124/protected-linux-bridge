package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** A root wrapper's zero exit alone cannot authorize the guarded VM launch. */
public final class AndroidProfileCheckTest {
  @Test
  public void acceptsOnlyMatchingKnownProfileMarkers() {
    assertTrue(
        AndroidGuestRuntime.verifiedCheck(0, "ANDROID_PROFILE_CHECK_OK memory_mib=512\n", 512));
    assertTrue(
        AndroidGuestRuntime.verifiedCheck(0, "ANDROID_PROFILE_CHECK_OK memory_mib=1536\n", 1536));
    assertTrue(
        AndroidGuestRuntime.verifiedCheck(0, "ANDROID_PROFILE_CHECK_OK memory_mib=1792\n", 1792));
    assertFalse(
        AndroidGuestRuntime.verifiedCheck(0, "ANDROID_PROFILE_CHECK_OK memory_mib=1536\n", 1792));
    assertFalse(
        AndroidGuestRuntime.verifiedCheck(42, "ANDROID_PROFILE_CHECK_OK memory_mib=1792\n", 1792));
    assertFalse(
        AndroidGuestRuntime.verifiedCheck(0, "ANDROID_PROFILE_CHECK_OK memory_mib=512\n", 1536));
    assertFalse(
        AndroidGuestRuntime.verifiedCheck(0, "ANDROID_PROFILE_CHECK_OK memory_mib=2048\n", 2048));
  }

  @Test
  public void rejectsDenialEmptyOrAmbiguousOutputEvenWhenWrapperExitsZero() {
    for (String output :
        new String[] {
          null, "", "not allowed to elevate", "ANDROID_PROFILE_CHECK_OK memory_mib=1536\nextra"
        }) {
      assertFalse(AndroidGuestRuntime.verifiedCheck(0, output, 1536));
    }
  }

  @Test
  public void rejectsFailedOrOversizedChecks() {
    String marker = "ANDROID_PROFILE_CHECK_OK memory_mib=1536\n";
    assertFalse(AndroidGuestRuntime.verifiedCheck(39, marker, 1536));
    assertFalse(AndroidGuestRuntime.verifiedCheck(0, marker + " ".repeat(4096), 1536));
  }
}
