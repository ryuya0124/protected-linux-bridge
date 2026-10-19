package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.SurfaceTexture;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.PixelCopy;
import android.view.Surface;
import android.view.TextureView;
import android.view.WindowManager;
import android.widget.FrameLayout;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Developer-only, short-lived host Surface. Does not launch or change a VM. */
public final class ProbeActivity extends Activity implements TextureView.SurfaceTextureListener {
  static volatile Surface surface;
  static volatile int sampleStatus = -1, sampleColor;
  // Retain the producer queue across Activity recreation. A native window
  // belongs to this queue, not to the current TextureView instance.
  private static SurfaceTexture retainedTexture;
  private static volatile LocalSocket control;
  private static volatile boolean closed;
  private static boolean sessionStarted;
  private final Handler handler = new Handler(Looper.getMainLooper());

  @Override
  public void onCreate(Bundle state) {
    super.onCreate(state);
    if (!android.os.Build.MODEL.equals("SM-S948Q")) {
      finish();
      return;
    }
    TextureView view = new TextureView(this);
    view.setSurfaceTextureListener(this);
    if (retainedTexture != null) view.setSurfaceTexture(retainedTexture);
    FrameLayout frame = new FrameLayout(this);
    frame.setBackgroundColor(android.graphics.Color.BLACK);
    frame.addView(view, new FrameLayout.LayoutParams(480, 800, Gravity.CENTER));
    frame.addOnLayoutChangeListener(
        (parent, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
          int width = Math.min(right - left, (bottom - top) * 480 / 800);
          if (width > 0)
            view.setLayoutParams(
                new FrameLayout.LayoutParams(width, width * 800 / 480, Gravity.CENTER));
        });
    setContentView(frame);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    if (getIntent().getBooleanExtra("vmm", false) && !sessionStarted) {
      sessionStarted = true;
      new Thread(this::session, "plb-surface-control").start();
      new Handler(Looper.getMainLooper())
          .postDelayed(
              () -> {
                closed = true;
                closeControl();
                finish();
              },
              180000);
    } else if (!sessionStarted) {
      handler.postDelayed(this::finish, 15000);
    }
    handler.post(this::sample);
  }

  @Override
  public void onSurfaceTextureAvailable(SurfaceTexture texture, int width, int height) {
    if (retainedTexture == null) {
      texture.setDefaultBufferSize(480, 800);
      retainedTexture = texture;
      surface = new Surface(texture);
    }
  }

  @Override
  public void onSurfaceTextureSizeChanged(SurfaceTexture texture, int width, int height) {}

  @Override
  public boolean onSurfaceTextureDestroyed(SurfaceTexture texture) {
    // TextureView releases its GL attachment. Keep the BufferQueue and Surface
    // so the same renderer can resume when another view attaches the texture.
    return false;
  }

  @Override
  public void onSurfaceTextureUpdated(SurfaceTexture texture) {}

  private void sample() {
    Surface current = surface;
    if (isFinishing() || isDestroyed()) return;
    if (current == null || !current.isValid()) {
      handler.postDelayed(this::sample, 200);
      return;
    }
    Bitmap pixel = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888);
    PixelCopy.request(
        current,
        pixel,
        result -> {
          sampleStatus = result;
          if (result == PixelCopy.SUCCESS) sampleColor = pixel.getPixel(0, 0);
          pixel.recycle();
          handler.postDelayed(this::sample, 200);
        },
        handler);
  }

  private static int number(InputStream input) throws IOException {
    byte[] bytes = input.readNBytes(4);
    if (bytes.length != 4) throw new IOException("short greeting");
    return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).getInt();
  }

  private void session() {
    try {
      LocalSocket socket = null;
      for (int i = 0; i < 300 && !closed; i++) {
        socket = new LocalSocket();
        try {
          socket.connect(new LocalSocketAddress("net.ryuya_dev.protectedlinux.android-display"));
          break;
        } catch (IOException unavailable) {
          socket.close();
          socket = null;
          Thread.sleep(100);
        }
      }
      if (socket == null || closed) return;
      control = socket;
      socket.setSoTimeout(120000);
      var input = socket.getInputStream();
      var output = socket.getOutputStream();
      if (number(input) != 0x41424c50
          || number(input) != 1
          || number(input) != 480
          || number(input) != 800
          || number(input) != 0) throw new IOException("guest readiness");
      android.util.Log.i("PLBSurfaceProbe", "GUEST_READY_NO_CAPTURE");
      int pings = 0;
      while (!closed) {
        // Setup readiness precedes Android's window service. An early unlock
        // failure must not tear down the VM before its first frame arrives.
        if (++pings == 15) {
          output.write('U');
          android.util.Log.i("PLBSurfaceProbe", "GUEST_UNLOCK_RESULT " + input.read());
        }
        output.write('P');
        if (input.read() != 'P') throw new IOException("session ping");
        Thread.sleep(1000);
      }
    } catch (Exception error) {
      android.util.Log.w(
          "PLBSurfaceProbe",
          "SESSION_ENDED " + error.getClass().getSimpleName() + ": " + error.getMessage());
    } finally {
      closeControl();
    }
  }

  private static void closeControl() {
    LocalSocket socket = control;
    control = null;
    if (socket != null)
      try {
        socket.close();
      } catch (IOException ignored) {
      }
  }

  @Override
  protected void onDestroy() {
    // The bounded session owns control independently of this Activity.
    handler.removeCallbacksAndMessages(null);
    super.onDestroy();
  }
}
