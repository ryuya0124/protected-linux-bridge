package net.ryuya_dev.protectedlinux.surfaceprobe;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.PixelCopy;
import android.view.Surface;
import java.io.ByteArrayOutputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Private provider: only the explicitly started root probe can obtain the Surface. */
public final class SurfaceProvider extends ContentProvider {
  @Override
  public boolean onCreate() {
    return true;
  }

  @Override
  public Bundle call(String method, String argument, Bundle extras) {
    if (Binder.getCallingUid() != 0
        || (!"surface".equals(method) && !"sample".equals(method) && !"frame".equals(method))) {
      throw new SecurityException("root probe only");
    }
    Surface surface = ProbeActivity.surface;
    Bundle result = new Bundle();
    if ("frame".equals(method)) {
      if (surface == null || !surface.isValid()) return result;
      Bitmap image = Bitmap.createBitmap(480, 800, Bitmap.Config.ARGB_8888);
      CountDownLatch complete = new CountDownLatch(1);
      AtomicReference<byte[]> bytes = new AtomicReference<>();
      PixelCopy.request(
          surface,
          image,
          status -> {
            try {
              if (status == PixelCopy.SUCCESS) {
                ByteArrayOutputStream encoded = new ByteArrayOutputStream();
                if (image.compress(Bitmap.CompressFormat.PNG, 100, encoded)
                    && encoded.size() <= 524288) bytes.set(encoded.toByteArray());
              }
            } finally {
              image.recycle();
              complete.countDown();
            }
          },
          new Handler(Looper.getMainLooper()));
      try {
        if (complete.await(2, TimeUnit.SECONDS) && bytes.get() != null)
          result.putByteArray("png", bytes.get());
      } catch (InterruptedException interrupted) {
        Thread.currentThread().interrupt();
      }
      return result;
    }
    result.putInt("sample_status", ProbeActivity.sampleStatus);
    result.putInt("sample_color", ProbeActivity.sampleColor);
    if (surface != null && surface.isValid()) result.putParcelable("surface", surface);
    return result;
  }

  @Override
  public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
    throw new UnsupportedOperationException();
  }

  @Override
  public String getType(Uri uri) {
    return null;
  }

  @Override
  public Uri insert(Uri uri, ContentValues values) {
    throw new UnsupportedOperationException();
  }

  @Override
  public int delete(Uri uri, String selection, String[] args) {
    throw new UnsupportedOperationException();
  }

  @Override
  public int update(Uri uri, ContentValues values, String selection, String[] args) {
    throw new UnsupportedOperationException();
  }
}
