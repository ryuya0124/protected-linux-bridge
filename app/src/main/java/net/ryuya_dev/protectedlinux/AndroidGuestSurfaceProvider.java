package net.ryuya_dev.protectedlinux;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;

/** Root-only publication of one viewer's private display Binder. */
public final class AndroidGuestSurfaceProvider extends ContentProvider {
  @Override
  public boolean onCreate() {
    return true;
  }

  @Override
  public Bundle call(String method, String argument, Bundle extras) {
    int uid = Binder.getCallingUid(), pid = Binder.getCallingPid();
    if (uid != 0 || extras == null) throw new SecurityException("Owned root renderer only");
    OwnedDisplayConnection connection = OwnedDisplayConnection.current();
    if (connection == null) throw new IllegalStateException("No owned display viewer");
    String token = extras.getString("session");
    if (token == null || !token.matches("[0-9a-f]{64}"))
      throw new SecurityException("Display session");
    if ("display.claim".equals(method)) return connection.claim(uid, pid, token);
    try {
      if ("display.publish".equals(method))
        connection.publish(uid, pid, token, extras.getBinder("display"));
      else if ("display.clear".equals(method)) connection.revoke(uid, pid, token);
      else throw new SecurityException("Display method");
    } catch (android.os.RemoteException failure) {
      throw new IllegalStateException("Display handshake failed", failure);
    }
    Bundle result = new Bundle();
    result.putBoolean("accepted", true);
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
