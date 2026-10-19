package android.net;

import android.content.Context;
import android.os.Looper;

/** Compile-only signature verified against the selected guest; never packaged. */
public abstract class NetworkAgent {
  public NetworkAgent(
      Context context,
      Looper looper,
      String name,
      NetworkCapabilities capabilities,
      LinkProperties properties,
      int score,
      NetworkAgentConfig config,
      NetworkProvider provider) {
    throw new UnsupportedOperationException("Compile-only framework stub");
  }

  public Network register() {
    throw new UnsupportedOperationException();
  }

  public void markConnected() {
    throw new UnsupportedOperationException();
  }

  public void unregister() {
    throw new UnsupportedOperationException();
  }

  public void onNetworkUnwanted() {}

  public void onValidationStatus(int status, android.net.Uri redirect) {}
}
