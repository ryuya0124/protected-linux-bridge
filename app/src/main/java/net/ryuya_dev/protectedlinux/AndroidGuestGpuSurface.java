package net.ryuya_dev.protectedlinux;

import android.graphics.SurfaceTexture;
import android.view.Gravity;
import android.view.Surface;
import android.view.TextureView;
import android.widget.FrameLayout;
import java.io.IOException;

/** One VM's GPU producer queue. Keeping it across backgrounding avoids a stale native window. */
final class AndroidGuestGpuSurface implements TextureView.SurfaceTextureListener, AutoCloseable {
  private static AndroidGuestGpuSurface owner;
  private volatile Surface surface;
  private SurfaceTexture retained;
  final TextureView view;
  private long measuredAt;
  private int updates;
  private boolean closed;

  AndroidGuestGpuSurface(android.content.Context context, FrameLayout frame) throws IOException {
    synchronized (AndroidGuestGpuSurface.class) {
      if (owner != null) throw new IOException(context.getString(R.string.android_guest_busy));
      owner = this;
    }
    view = new TextureView(context);
    view.setSurfaceTextureListener(this);
    frame.addView(view, new FrameLayout.LayoutParams(1, 1, Gravity.CENTER));
    frame.addOnLayoutChangeListener(
        (parent, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
          int width = Math.min(right - left, (bottom - top) * 480 / 800);
          if (width > 0)
            view.setLayoutParams(
                new FrameLayout.LayoutParams(width, width * 800 / 480, Gravity.CENTER));
        });
  }

  static synchronized Surface current() {
    return owner == null ? null : owner.surface;
  }

  @Override
  public void onSurfaceTextureAvailable(SurfaceTexture texture, int width, int height) {
    if (closed) return;
    texture.setDefaultBufferSize(480, 800);
    retained = texture;
    surface = new Surface(texture);
  }

  @Override
  public void onSurfaceTextureSizeChanged(SurfaceTexture texture, int width, int height) {}

  @Override
  public boolean onSurfaceTextureDestroyed(SurfaceTexture texture) {
    // The viewer owns the queue until explicit close. TextureView releases its
    // GL attachment, then attaches again when the same Activity returns.
    return false;
  }

  @Override
  public void onSurfaceTextureUpdated(SurfaceTexture texture) {
    long now = android.os.SystemClock.elapsedRealtime();
    if (measuredAt == 0) measuredAt = now;
    updates++;
    if (now - measuredAt >= 5000) {
      // Texture latches are not proof of physical display presentation rate.
      android.util.Log.i(
          "ProtectedLinuxAndroid",
          "GPU_TEXTURE_STATS updates=" + updates + " elapsed_ms=" + (now - measuredAt));
      updates = 0;
      measuredAt = now;
    }
  }

  @Override
  public void close() {
    closed = true;
    synchronized (AndroidGuestGpuSurface.class) {
      if (owner == this) owner = null;
    }
    Surface current = surface;
    surface = null;
    if (current != null) current.release();
    if (retained != null) {
      retained.release();
      retained = null;
    }
  }
}
