package net.ryuya_dev.protectedlinux.gpuprobe;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class ProbeIdentityTest {
  private static final String NONCE = "0123456789abcdef0123456789abcdef";

  @Test
  public void acceptsOnlyThePinnedGuestWithAFreshWellFormedNonce() {
    assertTrue(ProbeIdentity.allowed("vsoc_arm64_only", "cutf_cvm", "16373615", NONCE));
    assertFalse(ProbeIdentity.allowed("vsoc_arm64_only", "cutf_cvm", "different", NONCE));
    assertFalse(ProbeIdentity.allowed("host", "cutf_cvm", "16373615", NONCE));
    assertFalse(ProbeIdentity.allowed("vsoc_arm64_only", "qcom", "16373615", NONCE));
  }

  @Test
  public void rejectsTheGalaxyHostAndMissingOrMalformedInputs() {
    assertFalse(ProbeIdentity.allowed("pa3q", "qcom", "S948QOPS1AZHL", NONCE));
    assertFalse(ProbeIdentity.allowed(null, null, null, NONCE));
    for (String nonce :
        new String[] {null, "", "a".repeat(31), "a".repeat(33), NONCE + "\n", "G".repeat(32)}) {
      assertFalse(ProbeIdentity.allowed("vsoc_arm64_only", "cutf_cvm", "16373615", nonce));
    }
  }
}
