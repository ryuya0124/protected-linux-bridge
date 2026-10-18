package net.ryuya_dev.protectedlinux;

import android.app.Activity;
import android.content.*;
import android.net.*;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.charset.*;
import java.util.concurrent.*;

/** Only explicit foreground button presses read or write the Android clipboard. */
final class ClipboardBridge {
  private static final int MAX = 65536;

  static void transfer(Activity a, boolean toLinux, java.util.function.Consumer<String> status) {
    boolean ja = a.getResources().getConfiguration().getLocales().get(0).getLanguage().equals("ja");
    if (!a.hasWindowFocus()) return;
    ClipboardManager clipboard = a.getSystemService(ClipboardManager.class);
    String source;
    try {
      if (toLinux) {
        ClipData data = clipboard.getPrimaryClip();
        if (data == null || data.getItemCount() < 1) {
          status.accept(ja ? "クリップボードが空です" : "Clipboard is empty");
          return;
        }
        CharSequence text = data.getItemAt(0).getText();
        if (text == null) throw new IOException();
        source = text.toString();
        if (source.getBytes(StandardCharsets.UTF_8).length > MAX) throw new IOException();
      } else source = null;
    } catch (Exception error) {
      status.accept(ja ? "共有できるのは64 KiB以下のテキストです" : "Share text up to 64 KiB");
      return;
    }
    status.accept(ja ? "共有しています…" : "Sharing…");
    ExecutorService worker = Executors.newSingleThreadExecutor();
    worker.submit(
        () -> {
          String received = null;
          boolean ok = false;
          try (LocalSocket socket = new LocalSocket()) {
            socket.connect(
                new LocalSocketAddress(
                    "net.ryuya_dev.protectedlinux.console", LocalSocketAddress.Namespace.ABSTRACT));
            if (socket.getPeerCredentials().getUid() != 0) throw new IOException();
            socket.setSoTimeout(12000);
            DataOutputStream out = new DataOutputStream(socket.getOutputStream());
            DataInputStream in = new DataInputStream(socket.getInputStream());
            out.writeByte('C');
            if (in.readUnsignedByte() != 0) throw new IOException();
            out.writeByte(toLinux ? 'P' : 'G');
            if (toLinux) {
              byte[] bytes = source.getBytes(StandardCharsets.UTF_8);
              out.writeInt(bytes.length);
              out.write(bytes);
              if (in.readUnsignedByte() != 0) throw new IOException();
            } else {
              int size = in.readInt();
              if (size < 0 || size > MAX) throw new IOException();
              byte[] bytes = new byte[size];
              in.readFully(bytes);
              received =
                  StandardCharsets.UTF_8
                      .newDecoder()
                      .onMalformedInput(CodingErrorAction.REPORT)
                      .decode(ByteBuffer.wrap(bytes))
                      .toString();
            }
            ok = true;
          } catch (Exception error) {
            android.util.Log.w(
                "ProtectedLinux", "CLIPBOARD_TRANSFER_FAILED " + error.getClass().getSimpleName());
          }
          String result = received;
          boolean success = ok;
          a.runOnUiThread(
              () -> {
                if (a.isDestroyed()) return;
                if (success && a.hasWindowFocus()) {
                  if (!toLinux) clipboard.setPrimaryClip(ClipData.newPlainText("Linux", result));
                  status.accept(ja ? "共有しました" : "Shared");
                } else
                  status.accept(
                      ja
                          ? "共有できませんでした。GUIと連携パッケージを確認してください"
                          : "Could not share. Check the GUI and integration packages.");
              });
          worker.shutdown();
        });
  }
}
