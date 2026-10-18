package net.ryuya_dev.protectedlinux;

import android.view.KeyEvent;
import android.view.MotionEvent;

/** Android key/button values to the X11 keysyms and RFB button mask. */
final class PhysicalInput {
  static int buttons(int state) {
    return ((state & MotionEvent.BUTTON_PRIMARY) != 0 ? 1 : 0)
        | ((state & MotionEvent.BUTTON_TERTIARY) != 0 ? 2 : 0)
        | ((state & MotionEvent.BUTTON_SECONDARY) != 0 ? 4 : 0);
  }

  static int unicodeMeta(int meta) {
    return meta & ~(KeyEvent.META_CTRL_MASK | KeyEvent.META_META_MASK);
  }

  static boolean hostKey(int code) {
    return switch (code) {
      case KeyEvent.KEYCODE_BACK,
          KeyEvent.KEYCODE_HOME,
          KeyEvent.KEYCODE_APP_SWITCH,
          KeyEvent.KEYCODE_POWER,
          KeyEvent.KEYCODE_VOLUME_UP,
          KeyEvent.KEYCODE_VOLUME_DOWN,
          KeyEvent.KEYCODE_VOLUME_MUTE,
          KeyEvent.KEYCODE_ASSIST,
          KeyEvent.KEYCODE_BRIGHTNESS_UP,
          KeyEvent.KEYCODE_BRIGHTNESS_DOWN ->
          true;
      default -> false;
    };
  }

  static int symbol(int key, int unicode) {
    if (key >= KeyEvent.KEYCODE_F1 && key <= KeyEvent.KEYCODE_F12)
      return 0xffbe + key - KeyEvent.KEYCODE_F1;
    int special =
        switch (key) {
          case KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> 0xff0d;
          case KeyEvent.KEYCODE_TAB -> 0xff09;
          case KeyEvent.KEYCODE_ESCAPE -> 0xff1b;
          case KeyEvent.KEYCODE_DEL -> 0xff08;
          case KeyEvent.KEYCODE_FORWARD_DEL -> 0xffff;
          case KeyEvent.KEYCODE_DPAD_LEFT -> 0xff51;
          case KeyEvent.KEYCODE_DPAD_UP -> 0xff52;
          case KeyEvent.KEYCODE_DPAD_RIGHT -> 0xff53;
          case KeyEvent.KEYCODE_DPAD_DOWN -> 0xff54;
          case KeyEvent.KEYCODE_MOVE_HOME -> 0xff50;
          case KeyEvent.KEYCODE_MOVE_END -> 0xff57;
          case KeyEvent.KEYCODE_PAGE_UP -> 0xff55;
          case KeyEvent.KEYCODE_PAGE_DOWN -> 0xff56;
          case KeyEvent.KEYCODE_INSERT -> 0xff63;
          case KeyEvent.KEYCODE_SHIFT_LEFT -> 0xffe1;
          case KeyEvent.KEYCODE_SHIFT_RIGHT -> 0xffe2;
          case KeyEvent.KEYCODE_CTRL_LEFT -> 0xffe3;
          case KeyEvent.KEYCODE_CTRL_RIGHT -> 0xffe4;
          case KeyEvent.KEYCODE_ALT_LEFT -> 0xffe9;
          case KeyEvent.KEYCODE_ALT_RIGHT -> 0xffea;
          case KeyEvent.KEYCODE_META_LEFT -> 0xffeb;
          case KeyEvent.KEYCODE_META_RIGHT -> 0xffec;
          case KeyEvent.KEYCODE_CAPS_LOCK -> 0xffe5;
          case KeyEvent.KEYCODE_NUM_LOCK -> 0xff7f;
          case KeyEvent.KEYCODE_SCROLL_LOCK -> 0xff14;
          case KeyEvent.KEYCODE_SYSRQ -> 0xff61;
          case KeyEvent.KEYCODE_BREAK -> 0xff13;
          case KeyEvent.KEYCODE_MENU -> 0xff67;
          case KeyEvent.KEYCODE_ZENKAKU_HANKAKU -> 0xff2a;
          case KeyEvent.KEYCODE_HENKAN -> 0xff23;
          case KeyEvent.KEYCODE_MUHENKAN -> 0xff22;
          case KeyEvent.KEYCODE_KATAKANA_HIRAGANA -> 0xff27;
          default -> 0;
        };
    if (special != 0) return special;
    if (unicode < 32 || unicode > 0x10ffff || (unicode >= 0xd800 && unicode <= 0xdfff)) return 0;
    return unicode <= 255 ? unicode : 0x01000000 | unicode;
  }
}
