package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import android.view.KeyEvent;
import android.view.MotionEvent;
import org.junit.Test;

public class PhysicalInputTest {
  @Test
  public void mouseRightAndMiddleAreNotSwapped() {
    assertEquals(4, PhysicalInput.buttons(MotionEvent.BUTTON_SECONDARY));
    assertEquals(2, PhysicalInput.buttons(MotionEvent.BUTTON_TERTIARY));
    assertEquals(
        5, PhysicalInput.buttons(MotionEvent.BUTTON_PRIMARY | MotionEvent.BUTTON_SECONDARY));
  }

  @Test
  public void modifiersAreSentAsSeparateKeysyms() {
    assertEquals(0xffe3, PhysicalInput.symbol(KeyEvent.KEYCODE_CTRL_LEFT, 0));
    assertEquals(0xffea, PhysicalInput.symbol(KeyEvent.KEYCODE_ALT_RIGHT, 0));
    assertEquals(
        KeyEvent.META_SHIFT_ON,
        PhysicalInput.unicodeMeta(
            KeyEvent.META_CTRL_ON | KeyEvent.META_META_ON | KeyEvent.META_SHIFT_ON));
  }

  @Test
  public void functionAndNavigationKeys() {
    assertEquals(0xffbe, PhysicalInput.symbol(KeyEvent.KEYCODE_F1, 0));
    assertEquals(0xffc9, PhysicalInput.symbol(KeyEvent.KEYCODE_F12, 0));
    assertEquals(0xff51, PhysicalInput.symbol(KeyEvent.KEYCODE_DPAD_LEFT, 0));
    assertEquals(0xffff, PhysicalInput.symbol(KeyEvent.KEYCODE_FORWARD_DEL, 0));
  }

  @Test
  public void unicodeAndJapaneseConversionKeys() {
    assertEquals(0x01003042, PhysicalInput.symbol(0, 0x3042));
    assertEquals(0xff23, PhysicalInput.symbol(KeyEvent.KEYCODE_HENKAN, 0));
    assertEquals(0x63, PhysicalInput.symbol(KeyEvent.KEYCODE_C, 'c'));
    assertEquals(0, PhysicalInput.symbol(0, 0xd800));
  }

  @Test
  public void phoneNavigationAndVolumeStayWithAndroid() {
    assertTrue(PhysicalInput.hostKey(KeyEvent.KEYCODE_BACK));
    assertTrue(PhysicalInput.hostKey(KeyEvent.KEYCODE_VOLUME_UP));
    assertFalse(PhysicalInput.hostKey(KeyEvent.KEYCODE_TAB));
  }
}
