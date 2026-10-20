package net.ryuya_dev.protectedlinux;

import android.app.Application;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import androidx.lifecycle.AndroidViewModel;
import androidx.lifecycle.MutableLiveData;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;
import net.ryuya_dev.protectedlinux.backup.BackupArchive;

/** Foreground maintenance across view recreation. Every completion belongs to one job. */
public final class BackupModel extends AndroidViewModel {
  record State(boolean busy, long bytes, String result, BackupArchive.Info preview) {}

  final MutableLiveData<State> state = new MutableLiveData<>(new State(false, 0, "", null));
  private final Handler main = new Handler(Looper.getMainLooper());
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private final AtomicLong generation = new AtomicLong();
  private Future<?> task;
  private volatile BackupClient client;
  private volatile InputStream inspecting;
  Uri restoring;
  Set<String> exporting = Set.of();
  private boolean busy;
  private long reported;

  public BackupModel(Application application) {
    super(application);
  }

  private long begin() {
    busy = true;
    reported = 0;
    long id = generation.incrementAndGet();
    state.setValue(new State(true, 0, "", null));
    return id;
  }

  void inspect(Uri document) {
    if (busy) return;
    long id = begin();
    task =
        worker.submit(
            () -> {
              try (InputStream input =
                  getApplication().getContentResolver().openInputStream(document)) {
                inspecting = input;
                if (input == null) throw new IOException();
                BackupArchive.Info info = BackupArchive.inspect(input);
                main.post(
                    () -> {
                      if (generation.get() == id) restoring = document;
                    });
                finish(id, "PREVIEW", info);
              } catch (Exception error) {
                finish(id, "ARCHIVE_FORMAT", null);
              } finally {
                inspecting = null;
              }
            });
  }

  void run(String action, Uri document, Set<String> selected) {
    if (busy) return;
    long id = begin();
    task =
        worker.submit(
            () -> {
              BackupClient current = new BackupClient();
              client = current;
              String result;
              try {
                switch (action) {
                  case "export" ->
                      current.export(
                          getApplication(), document, selected, bytes -> progress(id, bytes));
                  case "restore" ->
                      current.restore(
                          getApplication(), document, selected, bytes -> progress(id, bytes));
                  case "debian-download" ->
                      current.installDebian(getApplication(), true, bytes -> progress(id, bytes));
                  case "debian-import" ->
                      current.installDebian(getApplication(), false, bytes -> progress(id, bytes));
                  case "recover" -> current.recover(getApplication());
                  case "stop" -> {
                    if (!RuntimeSetup.stop(getApplication())) throw new IOException("ARCHIVE_STOP");
                  }
                  default -> throw new IOException("ARCHIVE_ACTION");
                }
                result = action.toUpperCase(Locale.ROOT) + "_DONE";
              } catch (Exception failure) {
                Throwable error =
                    failure instanceof ExecutionException && failure.getCause() != null
                        ? failure.getCause()
                        : failure;
                String message = error.getMessage();
                result =
                    Thread.currentThread().isInterrupted()
                        ? "ARCHIVE_CANCELLED"
                        : message != null && message.matches("(ARCHIVE|RESTORE)_[A-Z_]+")
                            ? message
                            : "ARCHIVE_IO";
              } finally {
                current.close();
                if (client == current) client = null;
              }
              finish(id, result, null);
            });
  }

  private void progress(long id, long bytes) {
    main.post(
        () -> {
          long now = android.os.SystemClock.elapsedRealtime();
          if (generation.get() != id || now - reported < 250) return;
          reported = now;
          state.setValue(new State(true, bytes, "", null));
        });
  }

  private void finish(long id, String result, BackupArchive.Info info) {
    main.post(
        () -> {
          if (generation.get() != id) return;
          busy = false;
          android.util.Log.i("ProtectedLinux", "ARCHIVE_JOB_RESULT " + result);
          state.setValue(new State(false, 0, result, info));
        });
  }

  void consumePreview() {
    State value = state.getValue();
    if (value != null && value.preview() != null) state.setValue(new State(false, 0, "", null));
  }

  void cancel() {
    long id = generation.incrementAndGet();
    if (task != null) task.cancel(true);
    BackupClient current = client;
    if (current != null) current.close();
    InputStream input = inspecting;
    if (input != null)
      try {
        input.close();
      } catch (IOException ignored) {
      }
    finish(id, "ARCHIVE_CANCELLED", null);
  }

  @Override
  protected void onCleared() {
    cancel();
    worker.shutdownNow();
  }
}
