package net.ryuya_dev.protectedlinux;

import android.content.Context;
import android.net.Uri;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.LongConsumer;
import net.ryuya_dev.protectedlinux.backup.*;

/** SAF streams never become shell paths. Only signed-APK code runs in the root worker. */
final class BackupClient implements AutoCloseable {
  private volatile Process process;
  private final java.util.concurrent.atomic.AtomicBoolean closed =
      new java.util.concurrent.atomic.AtomicBoolean();
  private final ExecutorService errors = Executors.newSingleThreadExecutor();
  private volatile String lastError = "ARCHIVE_IO";
  private static final java.util.concurrent.atomic.AtomicReference<BackupClient> ACTIVE =
      new java.util.concurrent.atomic.AtomicReference<>();

  static boolean busy() {
    return ACTIVE.get() != null;
  }

  private static String quote(String value) {
    return "'" + value.replace("'", "'\\''") + "'";
  }

  private Process start(Context context, String action, Set<String> selected, LongConsumer progress)
      throws Exception {
    if (closed.get() || Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
    if (!ACTIVE.compareAndSet(null, this)) throw new IOException("ARCHIVE_BUSY");
    try {
      if (android.os.Process.myUid() < 10000 || android.os.Process.myUid() > 19999)
        throw new IOException("ARCHIVE_USER");
      AndroidGuestRuntime.install(context, "libbackup-lock.so");
      if (!BackupArchive.ROOTS.containsAll(selected)) throw new IOException("ARCHIVE_SELECTION");
      String command =
          "CLASSPATH="
              + quote(context.getApplicationInfo().sourceDir)
              + " exec app_process /system/bin net.ryuya_dev.protectedlinux.backuproot.BackupRoot "
              + action
              + (selected.isEmpty() ? "" : " " + quote(String.join(",", new TreeSet<>(selected))));
      if (closed.get() || Thread.currentThread().isInterrupted())
        throw new InterruptedIOException();
      process = new ProcessBuilder("su", "-M", "-c", command).start();
      Process current = process;
      CompletableFuture<Void> ready = new CompletableFuture<>();
      errors.submit(
          () -> {
            try (BufferedReader reader =
                new BufferedReader(new InputStreamReader(current.getErrorStream()))) {
              String line;
              while ((line = reader.readLine()) != null) {
                if (line.startsWith("ARCHIVE_ERROR ")) lastError = line.substring(14);
                if (line.equals("ARCHIVE_READY")) ready.complete(null);
                if (line.matches("ARCHIVE_PROGRESS [0-9]{1,15}"))
                  progress.accept(Long.parseLong(line.substring(17)));
              }
            } catch (IOException ignored) {
            } finally {
              ready.completeExceptionally(new IOException(lastError));
            }
          });
      if (!action.equals("--recover")) ready.get(20, TimeUnit.SECONDS);
      return current;
    } catch (Exception failure) {
      close();
      throw failure;
    }
  }

  private void complete(Process current) throws Exception {
    if (!current.waitFor(10, TimeUnit.SECONDS)) throw new IOException("ARCHIVE_TIMEOUT");
    errors.shutdown();
    errors.awaitTermination(2, TimeUnit.SECONDS);
    if (current.exitValue() != 0)
      throw new IOException(current.exitValue() == 28 ? "ARCHIVE_BUSY" : lastError);
  }

  void export(Context context, Uri document, Set<String> selected, LongConsumer progress)
      throws Exception {
    Process current = start(context, "--export", selected, progress);
    try {
      try (DataOutputStream request = new DataOutputStream(current.getOutputStream())) {
        if (selected.contains("settings")) {
          byte[] settings =
              PreferenceSnapshot.encode(context.getSharedPreferences("config", 0).getAll());
          request.writeInt(settings.length);
          request.write(settings);
        }
      }
      try (InputStream source = current.getInputStream();
          OutputStream output = context.getContentResolver().openOutputStream(document, "wt")) {
        if (output == null) throw new IOException("ARCHIVE_DOCUMENT");
        copy(source, output);
      }
      complete(current);
    } finally {
      close();
    }
  }

  void restore(Context context, Uri document, Set<String> selected, LongConsumer progress)
      throws Exception {
    Process current = start(context, "--restore", selected, progress);
    try {
      try (InputStream input = context.getContentResolver().openInputStream(document)) {
        if (input == null) throw new IOException("ARCHIVE_DOCUMENT");
        ArchiveTransport.send(input, current.getOutputStream());
      }
      String status = commitPrepared(current);
      complete(current);
      if (!status.startsWith("RESTORED\n")) throw new IOException("ARCHIVE_RESPONSE");
      for (String line : status.split("\n"))
        if (line.startsWith("SETTINGS ")) {
          Map<String, Object> settings =
              PreferenceSnapshot.decode(Base64.getDecoder().decode(line.substring(9)));
          var editor = context.getSharedPreferences("config", 0).edit();
          for (var entry : settings.entrySet()) {
            if (entry.getValue() instanceof Boolean flag) editor.putBoolean(entry.getKey(), flag);
            if (entry.getValue() instanceof Integer number) editor.putInt(entry.getKey(), number);
          }
          if (!editor.commit()) throw new IOException("ARCHIVE_SETTINGS_COMMIT");
          if (context.getApplicationContext() instanceof BridgeApplication application)
            application.publish();
        }
    } finally {
      close();
    }
  }

  private String commitPrepared(Process current) throws IOException {
    // Read a fixed line without readahead; binary/personal settings are never logged.
    byte[] expected = "PREPARED\n".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    for (byte value : expected)
      if (current.getInputStream().read() != (value & 255)) throw new IOException(lastError);
    if (closed.get() || Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
    current.getOutputStream().write(ArchiveTransport.COMMIT);
    current.getOutputStream().flush();
    current.getOutputStream().close();
    byte[] response = current.getInputStream().readNBytes(131073);
    if (response.length > 131072) throw new IOException("ARCHIVE_RESPONSE");
    return new String(response, java.nio.charset.StandardCharsets.UTF_8);
  }

  void installDebian(Context context, boolean download, LongConsumer progress) throws Exception {
    Process current =
        start(context, download ? "--install-debian" : "--import-debian", Set.of(), progress);
    javax.net.ssl.HttpsURLConnection connection = null;
    try {
      if (download) {
        connection =
            (javax.net.ssl.HttpsURLConnection)
                new java.net.URI(net.ryuya_dev.protectedlinux.debian.DebianImage.URL)
                    .toURL()
                    .openConnection();
        connection.setInstanceFollowRedirects(false);
        connection.setConnectTimeout(30000);
        connection.setReadTimeout(30000);
        if (connection.getResponseCode() != 200
            || connection.getContentLengthLong()
                != net.ryuya_dev.protectedlinux.debian.DebianImage.ARCHIVE_BYTES)
          throw new IOException("ARCHIVE_IMAGE_DOWNLOAD");
        try (InputStream input = connection.getInputStream()) {
          ArchiveTransport.send(input, current.getOutputStream());
        }
      }
      String response = commitPrepared(current);
      complete(current);
      if (!response.trim().equals("INSTALLED")) throw new IOException("ARCHIVE_RESPONSE");
    } finally {
      if (connection != null) connection.disconnect();
      close();
    }
  }

  void recover(Context context) throws Exception {
    Process current = start(context, "--recover", Set.of(), bytes -> {});
    try {
      current.getOutputStream().close();
      String response =
          new String(
              current.getInputStream().readNBytes(4096), java.nio.charset.StandardCharsets.UTF_8);
      complete(current);
      if (!response.trim().equals("RECOVERED")) throw new IOException("ARCHIVE_RESPONSE");
    } finally {
      close();
    }
  }

  private static void copy(InputStream input, OutputStream output) throws IOException {
    byte[] data = new byte[65536];
    int count;
    while ((count = input.read(data)) != -1) {
      if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
      output.write(data, 0, count);
    }
  }

  public void close() {
    closed.set(true);
    Process current = process;
    if (current != null) {
      try {
        current.getOutputStream().close();
      } catch (IOException ignored) {
      }
      try {
        current.getInputStream().close();
      } catch (IOException ignored) {
      }
      if (current.isAlive()) current.destroy();
    }
    errors.shutdownNow();
    ACTIVE.compareAndSet(this, null);
  }
}
