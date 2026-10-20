package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import java.util.List;
import org.junit.Test;

public class AudioRoutePolicyTest {
  private final AudioRoutePolicy.Device
      speaker = new AudioRoutePolicy.Device(1, "speaker", true, false),
      usb = new AudioRoutePolicy.Device(2, "usb", false, true),
      bt = new AudioRoutePolicy.Device(3, "bluetooth", false, true);

  @Test
  public void hostModeKeepsLinuxOnBuiltin() throws Exception {
    assertEquals(1, AudioRoutePolicy.select(List.of(speaker, bt), "builtin"));
  }

  @Test
  public void oneExternalCanBeAutomatic() throws Exception {
    assertEquals(3, AudioRoutePolicy.select(List.of(speaker, bt), "auto"));
  }

  @Test
  public void multipleExternalRequiresExplicitChoice() {
    assertThrows(
        AudioRoutePolicy.Ambiguous.class,
        () -> AudioRoutePolicy.select(List.of(speaker, usb, bt), "auto"));
  }

  @Test
  public void missingExplicitDeviceDoesNotLeakToSpeaker() {
    assertThrows(
        AudioRoutePolicy.Missing.class, () -> AudioRoutePolicy.select(List.of(speaker), "usb"));
  }

  @Test
  public void explicitChoiceSurvivesOtherDeviceConnection() throws Exception {
    assertEquals(2, AudioRoutePolicy.select(List.of(speaker, usb, bt), "usb"));
  }

  @Test
  public void unplugWithAutomaticSelectionDoesNotFallBack() {
    assertThrows(
        AudioRoutePolicy.Missing.class, () -> AudioRoutePolicy.select(List.of(speaker), "auto"));
  }

  @Test
  public void duplicateIdentityIsNotArbitrarilySelected() {
    assertThrows(
        AudioRoutePolicy.Ambiguous.class,
        () ->
            AudioRoutePolicy.select(
                List.of(usb, new AudioRoutePolicy.Device(4, "usb", false, true)), "usb"));
  }

  @Test
  public void multipleBuiltinPortsRequireAnExplicitSelection() {
    var back = new AudioRoutePolicy.Device(4, "back", true, false);
    assertThrows(
        AudioRoutePolicy.Ambiguous.class,
        () -> AudioRoutePolicy.select(List.of(speaker, back), "builtin"));
  }

  @Test
  public void aBuiltinPortCanBeSelectedIndependently() throws Exception {
    var back = new AudioRoutePolicy.Device(4, "back", true, false);
    assertEquals(4, AudioRoutePolicy.select(List.of(speaker, back, usb), "back"));
  }
}
