package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import org.junit.Test;

public final class AndroidGuestTouchTest {
  @Test
  public void letterboxedSwipeRetainsStartEndAndDuration() {
    AndroidGuestTouch touch = new AndroidGuestTouch();
    assertTrue(touch.begin(500, 1700, 1000, 2000, 100));
    assertArrayEquals(
        new int[] {240, 736, 240, 160, 400}, touch.end(500, 500, 1000, 2000, 500, 10));
  }

  @Test
  public void borderCancelAndMissingDownNeverInjectTap() {
    AndroidGuestTouch touch = new AndroidGuestTouch();
    assertFalse(touch.begin(500, 10, 1000, 2000, 100));
    assertNull(touch.end(500, 500, 1000, 2000, 500, 10));
    assertTrue(touch.begin(240, 400, 480, 800, 100));
    touch.cancel();
    assertNull(touch.end(240, 100, 480, 800, 500, 10));
  }

  @Test
  public void smallMotionIsTapAndSwipeBoundsAreClamped() {
    AndroidGuestTouch touch = new AndroidGuestTouch();
    touch.begin(240, 400, 480, 800, 100);
    assertArrayEquals(new int[] {240, 400}, touch.end(241, 401, 480, 800, 200, 8));
    touch.begin(240, 400, 480, 800, 100);
    assertArrayEquals(
        new int[] {240, 400, 479, 0, 1000}, touch.end(2000, -2000, 480, 800, 5000, 8));
  }
}
