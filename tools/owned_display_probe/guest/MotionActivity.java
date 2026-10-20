package net.ryuya_dev.plbguestmotion;

import android.app.Activity;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.WindowManager;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** Guest-only GLES motion workload. Render counts are not presentation measurements. */
public final class MotionActivity extends Activity implements GLSurfaceView.Renderer {
  private GLSurfaceView surface;
  private String nonce;
  private int width = 1, height = 1, frames;
  private long started;

  @Override
  public void onCreate(Bundle saved) {
    super.onCreate(saved);
    nonce = getIntent().getStringExtra("nonce");
    if (nonce == null || !nonce.matches("[0-9a-f]{32}")) {
      finish();
      return;
    }
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    surface = new GLSurfaceView(this);
    surface.setEGLContextClientVersion(2);
    surface.setRenderer(this);
    surface
        .getHolder()
        .addCallback(
            new SurfaceHolder.Callback() {
              @Override
              public void surfaceCreated(SurfaceHolder holder) {
                holder
                    .getSurface()
                    .setFrameRate(
                        120f,
                        Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
                        Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
                Log.i(
                    "PLBGuestMotion",
                    "nonce=" + nonce + " display_hz=" + getDisplay().getRefreshRate());
              }

              @Override
              public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {}

              @Override
              public void surfaceDestroyed(SurfaceHolder holder) {}
            });
    setContentView(surface);
    new Handler(Looper.getMainLooper()).postDelayed(this::finish, 45000);
  }

  @Override
  public void onSurfaceCreated(GL10 ignored, EGLConfig config) {
    started = System.nanoTime();
    Log.i(
        "PLBGuestMotion", "nonce=" + nonce + " renderer=" + GLES20.glGetString(GLES20.GL_RENDERER));
  }

  @Override
  public void onSurfaceChanged(GL10 ignored, int w, int h) {
    width = w;
    height = h;
    GLES20.glViewport(0, 0, w, h);
  }

  @Override
  public void onDrawFrame(GL10 ignored) {
    int frame = frames++;
    GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
    GLES20.glClearColor((frame & 31) / 31f, ((frame >>> 5) & 31) / 31f, 0.25f, 1f);
    GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
    GLES20.glEnable(GLES20.GL_SCISSOR_TEST);
    GLES20.glScissor(frame % width, 0, Math.min(12, width), height);
    GLES20.glClearColor(1f, 1f, 1f, 1f);
    GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
    GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
    if (frames % 120 == 0)
      Log.i(
          "PLBGuestMotion",
          "nonce="
              + nonce
              + " rendered="
              + frames
              + " elapsed_ns="
              + (System.nanoTime() - started));
  }

  @Override
  protected void onPause() {
    if (surface != null) surface.onPause();
    super.onPause();
  }

  @Override
  protected void onResume() {
    super.onResume();
    if (surface != null) surface.onResume();
  }
}
