package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import org.junit.Test;

public class TerminalSetupTest {
  @Test
  public void completionMarkerIsPositiveStateWithoutLaunching() {
    assertEquals(
        TerminalSetup.Result.ALREADY_INSTALLED,
        TerminalSetup.parse(0, "Package enabled\nPLB_TERMINAL_SETUP ALREADY_INSTALLED\n"));
  }

  @Test
  public void rootDenialAndUnconfirmedLaunchNeverReportSuccess() {
    assertEquals(TerminalSetup.Result.FAILED, TerminalSetup.parse(0, "Permission denied\n"));
    assertEquals(
        TerminalSetup.Result.FAILED, TerminalSetup.parse(0, "PLB_TERMINAL_SETUP OPENED\n"));
    assertEquals(
        TerminalSetup.Result.FAILED,
        TerminalSetup.parse(0, "Error: Activity not started\nPLB_TERMINAL_SETUP OPENED\n"));
  }

  @Test
  public void activityManagerMustConfirmLaunch() {
    assertEquals(
        TerminalSetup.Result.OPENED,
        TerminalSetup.parse(0, "Status: ok\nComplete\nPLB_TERMINAL_SETUP OPENED\n"));
  }

  @Test
  public void rejectedDuplicateAndNonzeroReceipts() {
    assertEquals(
        TerminalSetup.Result.FAILED, TerminalSetup.parse(1, "PLB_TERMINAL_SETUP ENABLED\n"));
    assertEquals(
        TerminalSetup.Result.FAILED,
        TerminalSetup.parse(0, "PLB_TERMINAL_SETUP ENABLED\nPLB_TERMINAL_SETUP OPENED\n"));
    assertEquals(
        TerminalSetup.Result.FAILED, TerminalSetup.parse(0, "PLB_TERMINAL_SETUP UNKNOWN\n"));
  }
}
