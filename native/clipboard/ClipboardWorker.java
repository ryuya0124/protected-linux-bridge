package net.ryuya_dev.protectedlinux.root;

import android.content.*;
import android.net.*;
import android.os.*;
import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.concurrent.*;

/** Opt-in shell-UID worker using its existing READ_CLIPBOARD_IN_BACKGROUND grant. */
public final class ClipboardWorker {
  static final int MAX = 65536;
  static int module, mode;
  static ClipboardManager clipboard;
  static volatile byte[] last;
  static volatile boolean stopped;
  static final ExecutorService send =
      new ThreadPoolExecutor(
          1,
          1,
          0L,
          TimeUnit.MILLISECONDS,
          new ArrayBlockingQueue<>(1),
          new ThreadPoolExecutor.DiscardOldestPolicy());

  static void state(String message) {
    System.out.println(message);
    System.out.flush();
  }

  static byte[] hash(byte[] b) throws Exception {
    return MessageDigest.getInstance("SHA-256").digest(b);
  }

  static boolean same(byte[] a, byte[] b) {
    return a != null && MessageDigest.isEqual(a, b);
  }

  static LocalSocket guest(char kind) throws IOException {
    LocalSocket s = new LocalSocket();
    try {
      s.connect(
          new LocalSocketAddress(
              "net.ryuya_dev.protectedlinux.console", LocalSocketAddress.Namespace.ABSTRACT));
      if (s.getPeerCredentials().getUid() != 0) throw new IOException();
      s.setSoTimeout(10000);
      s.getOutputStream().write(kind);
      if (s.getInputStream().read() != 0) throw new IOException();
      return s;
    } catch (IOException error) {
      s.close();
      throw error;
    }
  }

  static void androidChanged() {
    if ((mode & 1) == 0 || stopped) return;
    send.submit(
        () -> {
          try {
            ClipDescription description = clipboard.getPrimaryClipDescription();
            if (description != null
                && description.getExtras() != null
                && description.getExtras().getBoolean(ClipDescription.EXTRA_IS_SENSITIVE, false))
              return;
            ClipData clip = clipboard.getPrimaryClip();
            if (clip == null || clip.getItemCount() < 1) return;
            CharSequence text = clip.getItemAt(0).getText();
            if (text == null) return;
            byte[] bytes = text.toString().getBytes(StandardCharsets.UTF_8);
            if (bytes.length > MAX) return;
            byte[] digest = hash(bytes);
            if (same(last, digest)) return;
            try (LocalSocket s = guest('C')) {
              DataOutputStream out = new DataOutputStream(s.getOutputStream());
              out.writeByte('P');
              out.writeInt(bytes.length);
              out.write(bytes);
              if (s.getInputStream().read() != 0) return;
            }
            last = digest;
          } catch (Exception error) {
            state("CLIPBOARD_ANDROID_DEFERRED " + error.getClass().getSimpleName());
          }
        });
  }

  static void linuxWatch() {
    int delay = 1;
    while (!stopped) {
      try (LocalSocket s = guest('W')) {
        s.setSoTimeout(0);
        if ((mode & 1) != 0) androidChanged();
        DataInputStream in = new DataInputStream(s.getInputStream());
        delay = 1;
        while (!stopped) {
          int size = in.readInt();
          if (size < 0 || size > MAX) throw new IOException();
          byte[] bytes = new byte[size];
          in.readFully(bytes);
          String text =
              StandardCharsets.UTF_8
                  .newDecoder()
                  .onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
                  .decode(java.nio.ByteBuffer.wrap(bytes))
                  .toString();
          byte[] digest = hash(bytes);
          if (same(last, digest)) continue;
          last = digest;
          clipboard.setPrimaryClip(ClipData.newPlainText("Linux VM", text));
        }
      } catch (Exception error) {
        if (!stopped) state("CLIPBOARD_LINUX_DEFERRED " + error.getClass().getSimpleName());
      }
      try {
        Thread.sleep(delay * 1000L);
      } catch (InterruptedException e) {
        return;
      }
      delay = Math.min(30, delay * 2);
    }
  }

  public static void main(String[] args) throws Exception {
    if (android.os.Process.myUid() != 2000 || args.length != 2)
      throw new SecurityException("Shell UID required");
    module = Integer.parseInt(args[0]);
    mode = Integer.parseInt(args[1]);
    if (module < 10000 || module > 19999 || mode < 1 || mode > 3)
      throw new SecurityException("Invalid options");
    Looper.prepareMainLooper();
    Class<?> activityThread = Class.forName("android.app.ActivityThread");
    Method main = activityThread.getDeclaredMethod("systemMain");
    main.setAccessible(true);
    Object thread = main.invoke(null);
    Method contextMethod = activityThread.getDeclaredMethod("getSystemContext");
    contextMethod.setAccessible(true);
    Context context =
        ((Context) contextMethod.invoke(thread)).createPackageContext("com.android.shell", 0);
    for (String name : new String[] {"mOpPackageName", "mAttributionSource"}) {
      Field field = context.getClass().getDeclaredField(name);
      field.setAccessible(true);
      field.set(
          context,
          name.equals("mOpPackageName")
              ? "com.android.shell"
              : new AttributionSource.Builder(android.os.Process.myUid())
                  .setPackageName("com.android.shell")
                  .build());
    }
    if (!"com.android.shell".equals(context.getOpPackageName())
        || context.getAttributionSource().getUid() != 2000
        || !"com.android.shell".equals(context.getAttributionSource().getPackageName()))
      throw new SecurityException("Invalid shell attribution");
    if (context.checkSelfPermission("android.permission.READ_CLIPBOARD_IN_BACKGROUND")
        != android.content.pm.PackageManager.PERMISSION_GRANTED)
      throw new SecurityException("Background clipboard permission unavailable");
    clipboard = context.getSystemService(ClipboardManager.class);
    if ((mode & 1) != 0) clipboard.addPrimaryClipChangedListener(ClipboardWorker::androidChanged);
    if ((mode & 2) != 0) new Thread(ClipboardWorker::linuxWatch, "linux-clipboard-events").start();
    LocalServerSocket server = new LocalServerSocket("net.ryuya_dev.protectedlinux.clipboard");
    new Thread(
            () -> {
              while (!stopped)
                try (LocalSocket s = server.accept()) {
                  if (s.getPeerCredentials().getUid() != module) continue;
                  s.setSoTimeout(2000);
                  if (s.getInputStream().read() == '?') s.getOutputStream().write(mode);
                } catch (IOException ignored) {
                }
            },
            "clipboard-status")
        .start();
    state("CLIPBOARD_WORKER_READY mode=" + mode);
    if ((mode & 2) == 0) androidChanged();
    Looper.loop();
  }
}
