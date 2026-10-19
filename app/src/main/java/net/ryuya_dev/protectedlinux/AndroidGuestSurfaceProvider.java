package net.ryuya_dev.protectedlinux;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.view.Surface;

/** Root-only Binder transfer of the explicitly selected viewer's GPU Surface. */
public final class AndroidGuestSurfaceProvider extends ContentProvider {
  @Override
  public boolean onCreate() {
    return true;
  }

  @Override
  public Bundle call(String method, String argument, Bundle extras) {
    if (Binder.getCallingUid() != 0 || !"surface".equals(method))
      throw new SecurityException("Root renderer only");
    Bundle result = new Bundle();
    Surface surface = AndroidGuestGpuSurface.current();
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
