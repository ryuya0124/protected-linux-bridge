package net.ryuya_dev.protectedlinux;

import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.FrameLayout;
import java.io.IOException;

/** One VM's parent layer, suitable for the native SurfaceControl/AHB backend. */
final class AndroidGuestGpuSurface implements SurfaceHolder.Callback, AutoCloseable {
  private static AndroidGuestGpuSurface owner;
  private final OwnedDisplayConnection connection;
  private long generation;
  final SurfaceView view;
  private boolean closed;

  AndroidGuestGpuSurface(android.content.Context context, FrameLayout frame) throws IOException {
    connection = new OwnedDisplayConnection();
    synchronized (AndroidGuestGpuSurface.class) {
      if (owner != null) {
        connection.close();
        throw new IOException(context.getString(R.string.android_guest_busy));
      }
      owner = this;
    }
    view = new SurfaceView(context);
    view.getHolder().setFixedSize(480, 800);
    view.getHolder().addCallback(this);
    frame.addView(view, new FrameLayout.LayoutParams(1, 1, Gravity.CENTER));
    frame.addOnLayoutChangeListener(
        (parent, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
          int width = Math.min(right - left, (bottom - top) * 480 / 800);
          if (width > 0)
            view.setLayoutParams(
                new FrameLayout.LayoutParams(width, width * 800 / 480, Gravity.CENTER));
        });
  }

  @Override
  public void surfaceCreated(SurfaceHolder holder) {
    attach(holder);
  }

  @Override
  public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    attach(holder);
  }

  private void attach(SurfaceHolder holder) {
    if (closed) return;
    long replacement = connection.attach(holder.getSurface(), false);
    if (replacement > 0) generation = replacement;
  }

  @Override
  public void surfaceDestroyed(SurfaceHolder holder) {
    connection.detach(generation, false);
    generation = 0;
    // The Surface belongs to the holder. Only the worker's independent copy is released.
  }

  @Override
  public void close() {
    if (closed) return;
    closed = true;
    view.getHolder().removeCallback(this);
    synchronized (AndroidGuestGpuSurface.class) {
      if (owner == this) owner = null;
    }
    connection.close();
  }
}
