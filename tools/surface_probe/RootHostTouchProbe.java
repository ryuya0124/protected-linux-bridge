package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.app.ActivityManager;
import android.content.Context;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.os.Looper;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;

/** Bounded developer test. Inject only while Bridge's own guest viewer is focused. */
public final class RootHostTouchProbe {
  private static Context context;
  private static Object input;
  private static java.lang.reflect.Method inject;
  private static int targetUid;

  private static boolean focused() {
    var tasks = context.getSystemService(ActivityManager.class).getRunningTasks(1);
    return !tasks.isEmpty()
        && tasks.get(0).topActivity != null
        && tasks.get(0).topActivity.getPackageName().equals("net.ryuya_dev.protectedlinux")
        && tasks
            .get(0)
            .topActivity
            .getClassName()
            .equals("net.ryuya_dev.protectedlinux.AndroidGuestActivity");
  }

  private static void event(
      long down, int action, int count, float x0, float y0, float x1, float y1) {
    if (!focused()) throw new SecurityException("Viewer lost focus");
    MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[count];
    MotionEvent.PointerCoords[] coordinates = new MotionEvent.PointerCoords[count];
    for (int i = 0; i < count; i++) {
      properties[i] = new MotionEvent.PointerProperties();
      properties[i].id = i;
      properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
      coordinates[i] = new MotionEvent.PointerCoords();
      coordinates[i].x = i == 0 ? x0 : x1;
      coordinates[i].y = i == 0 ? y0 : y1;
      coordinates[i].pressure = 1;
      coordinates[i].size = 0.05f;
    }
    MotionEvent event =
        MotionEvent.obtain(
            down,
            SystemClock.uptimeMillis(),
            action,
            count,
            properties,
            coordinates,
            0,
            0,
            1,
            1,
            0,
            0,
            InputDevice.SOURCE_TOUCHSCREEN,
            0);
    try {
      try {
        if (!Boolean.TRUE.equals(inject.invoke(input, event, 2, targetUid)))
          throw new IllegalStateException("Input refused");
      } catch (ReflectiveOperationException failure) {
        throw new IllegalStateException("Targeted input API", failure);
      }
    } finally {
      event.recycle();
    }
  }

  private static void run(int left, int top, int right, int bottom) {
    float cx = (left + right) / 2f, cy = (top + bottom) / 2f;
    float dx = (right - left) / 8f, dy = (bottom - top) / 8f;
    long down = SystemClock.uptimeMillis();
    boolean complete = false;
    try {
      event(down, 0, 1, cx - dx, cy - dy, 0, 0);
      SystemClock.sleep(80);
      event(down, 5 | (1 << 8), 2, cx - dx, cy - dy, cx + dx, cy + dy);
      for (int i = 0; i < 20; i++) {
        float scale = 1 + i / 20f;
        event(down, 2, 2, cx - dx * scale, cy - dy * scale, cx + dx * scale, cy + dy * scale);
        SystemClock.sleep(16);
      }
      event(down, 6 | (1 << 8), 2, cx - dx * 2, cy - dy * 2, cx + dx * 2, cy + dy * 2);
      event(down, 1, 1, cx - dx * 2, cy - dy * 2, 0, 0);
      for (int i = 0; i < 2; i++) {
        SystemClock.sleep(80);
        down = SystemClock.uptimeMillis();
        event(down, 0, 1, cx, cy, 0, 0);
        SystemClock.sleep(40);
        event(down, 1, 1, cx, cy, 0, 0);
      }
      down = SystemClock.uptimeMillis();
      event(down, 0, 1, cx, cy, 0, 0);
      event(down, 3, 1, cx, cy, 0, 0);
      complete = true;
      System.out.println("PLB_HOST_VIEWER_TOUCH_TEST_COMPLETE");
    } finally {
      if (!complete && focused()) event(down, 3, 1, cx, cy, 0, 0);
    }
  }

  public static void main(String[] args) {
    try {
      RootVmmLoadProbe.verifyHost();
      if (args.length != 4) throw new IllegalArgumentException("Viewer rectangle");
      int[] bounds = new int[4];
      for (int i = 0; i < 4; i++) bounds[i] = Integer.parseInt(args[i]);
      Looper.prepareMainLooper();
      Object thread =
          Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
      context = (Context) thread.getClass().getMethod("getSystemContext").invoke(thread);
      Class<?> inputType = Class.forName("android.hardware.input.InputManagerGlobal");
      input = inputType.getMethod("getInstance").invoke(null);
      inject =
          inputType.getMethod(
              "injectInputEvent", android.view.InputEvent.class, int.class, int.class);
      targetUid =
          context.getPackageManager().getApplicationInfo("net.ryuya_dev.protectedlinux", 0).uid;
      if (targetUid < 10000 || targetUid > 19999) throw new SecurityException("Own target UID");
      Point size = new Point();
      context.getSystemService(DisplayManager.class).getDisplay(0).getRealSize(size);
      if (bounds[0] < 0
          || bounds[1] < 0
          || bounds[2] > size.x
          || bounds[3] > size.y
          || bounds[2] - bounds[0] < 300
          || bounds[3] - bounds[1] < 300
          || !focused()) throw new SecurityException("Own viewer bounds");
      Throwable[] failure = new Throwable[1];
      Thread test =
          new Thread(
              () -> {
                try {
                  run(bounds[0], bounds[1], bounds[2], bounds[3]);
                } catch (Throwable error) {
                  failure[0] = error;
                }
              },
              "plb-own-viewer-input-test");
      test.start();
      test.join(10000);
      if (test.isAlive() || failure[0] != null)
        throw new IllegalStateException("Touch test failed", failure[0]);
    } catch (Throwable error) {
      error.printStackTrace();
      System.exit(30);
    }
    System.exit(0);
  }
}
