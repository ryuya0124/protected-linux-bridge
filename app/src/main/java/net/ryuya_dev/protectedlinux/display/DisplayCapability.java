package net.ryuya_dev.protectedlinux.display;

import java.io.*;
import java.util.HexFormat;

/** Fixed-size capability for the launcher's private stdin pipe, never argv or logs. */
public final class DisplayCapability {
  public final int viewerUid;
  public final String session;

  public DisplayCapability(int uid, String token) {
    if (uid < 10000 || uid > 19999 || token == null || !token.matches("[0-9a-f]{64}"))
      throw new IllegalArgumentException("Display capability");
    viewerUid = uid;
    session = token;
  }

  public void write(OutputStream stream) throws IOException {
    DataOutputStream out = new DataOutputStream(stream);
    out.writeInt(0x504c4431);
    out.writeInt(viewerUid);
    out.write(HexFormat.of().parseHex(session));
    out.flush();
  }

  public static DisplayCapability read(InputStream stream) throws IOException {
    DataInputStream in = new DataInputStream(stream);
    if (in.readInt() != 0x504c4431) throw new IOException("Display protocol");
    int uid = in.readInt();
    byte[] bytes = new byte[32];
    in.readFully(bytes);
    try {
      return new DisplayCapability(uid, HexFormat.of().formatHex(bytes));
    } catch (IllegalArgumentException invalid) {
      throw new IOException("Display identity", invalid);
    }
  }
}
