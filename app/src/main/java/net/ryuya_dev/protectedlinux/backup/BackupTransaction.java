package net.ryuya_dev.protectedlinux.backup;

import java.io.*;
import java.nio.channels.FileChannel;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;

/** Journalled directory promotion. The caller must hold the native VM owner flock. */
public final class BackupTransaction {
  private final Path root, journal, previous;

  public BackupTransaction(Path root) {
    this.root = root;
    journal = root.resolve("vm-owner.lock.restore-pending");
    previous = root.resolve("backup-recovery");
  }

  private static void sync(Path directory) throws IOException {
    try (FileChannel channel = FileChannel.open(directory, StandardOpenOption.READ)) {
      channel.force(true);
    }
  }

  private void validate(String group) throws IOException {
    if (!BackupArchive.ROOTS.contains(group) || group.equals("settings"))
      throw new IOException("RESTORE_ROOT");
  }

  private static boolean exists(Path path) {
    return Files.exists(path, LinkOption.NOFOLLOW_LINKS);
  }

  private static void directory(Path path) throws IOException {
    if (exists(path) && !Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS))
      throw new IOException("RESTORE_TYPE");
  }

  public void promote(Path stage, Set<String> groups) throws IOException {
    if (exists(journal)) throw new IOException("RESTORE_PENDING");
    if (groups.isEmpty()) return;
    directory(previous);
    Files.createDirectories(previous);
    Files.setPosixFilePermissions(previous, PosixFilePermissions.fromString("rwx------"));
    String id = UUID.randomUUID().toString();
    Path saved = previous.resolve(id);
    Files.createDirectory(
        saved, PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));
    Properties state = new Properties();
    state.setProperty("schema", "1");
    state.setProperty("id", id);
    for (String group : groups) {
      validate(group);
      directory(root.resolve(group));
      directory(stage.resolve(group));
      if (!exists(stage.resolve(group))) throw new IOException("RESTORE_STAGE");
      state.setProperty(group, exists(root.resolve(group)) ? "present" : "absent");
    }
    Path pending = saved.resolve("journal.pending");
    try (OutputStream output =
        Files.newOutputStream(
            pending,
            StandardOpenOption.CREATE_NEW,
            StandardOpenOption.WRITE,
            LinkOption.NOFOLLOW_LINKS)) {
      state.store(output, "Interrupted restore must roll back before any VM startup");
    }
    try (FileChannel file =
        FileChannel.open(pending, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS)) {
      file.force(true);
    }
    sync(saved);
    sync(previous);
    Files.move(pending, journal, StandardCopyOption.ATOMIC_MOVE);
    sync(root);
    // From this point the durable marker blocks all native profile-lock clients.
    try {
      for (String group : new TreeSet<>(groups)) {
        if (exists(root.resolve(group))) {
          Files.move(root.resolve(group), saved.resolve(group), StandardCopyOption.ATOMIC_MOVE);
          sync(root);
          sync(saved);
        }
        Files.move(stage.resolve(group), root.resolve(group), StandardCopyOption.ATOMIC_MOVE);
        sync(stage);
        sync(root);
      }
      // A successful restore keeps the prior directories. Nothing is deleted automatically.
      Files.copy(journal, saved.resolve("restore-record.properties"));
      try (FileChannel file =
          FileChannel.open(saved.resolve("restore-record.properties"), StandardOpenOption.WRITE)) {
        file.force(true);
      }
      sync(saved);
      Files.delete(journal);
      sync(root);
    } catch (IOException failure) {
      try {
        recover();
      } catch (IOException recovery) {
        failure.addSuppressed(recovery);
      }
      throw failure;
    }
  }

  public boolean recover() throws IOException {
    if (!exists(journal)) return false;
    if (!Files.isRegularFile(journal, LinkOption.NOFOLLOW_LINKS) || Files.size(journal) > 65536)
      throw new IOException("RESTORE_JOURNAL");
    Properties state = new Properties();
    try (InputStream input = Files.newInputStream(journal, LinkOption.NOFOLLOW_LINKS)) {
      state.load(input);
    }
    String id = state.getProperty("id", "");
    if (!state.getProperty("schema", "").equals("1")
        || !id.matches("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}"))
      throw new IOException("RESTORE_JOURNAL");
    Path saved = previous.resolve(id), failed = saved.resolve("interrupted-new-data");
    directory(previous);
    directory(saved);
    directory(failed);
    if (!exists(saved)) throw new IOException("RESTORE_PREVIOUS_MISSING");
    // Validate the entire journal before changing any directory.
    for (String group : state.stringPropertyNames()) {
      if (group.equals("schema") || group.equals("id")) continue;
      validate(group);
      if (!Set.of("present", "absent").contains(state.getProperty(group)))
        throw new IOException("RESTORE_JOURNAL");
      directory(saved.resolve(group));
      directory(root.resolve(group));
    }
    for (String group : new TreeSet<>(state.stringPropertyNames())) {
      if (group.equals("schema") || group.equals("id")) continue;
      validate(group);
      String before = state.getProperty(group);
      if (!Set.of("present", "absent").contains(before)) throw new IOException("RESTORE_JOURNAL");
      Path original = saved.resolve(group), current = root.resolve(group);
      directory(original);
      directory(current);
      if (exists(original) || before.equals("absent")) {
        if (exists(current)) {
          Files.createDirectories(failed);
          if (exists(failed.resolve(group))) throw new IOException("RESTORE_AMBIGUOUS");
          Files.move(current, failed.resolve(group), StandardCopyOption.ATOMIC_MOVE);
          sync(failed);
          sync(root);
        }
        if (exists(original)) {
          Files.move(original, current, StandardCopyOption.ATOMIC_MOVE);
          sync(saved);
          sync(root);
        }
      } else if (!exists(current)) throw new IOException("RESTORE_ORIGINAL_MISSING");
    }
    Files.delete(journal);
    sync(root);
    return true;
  }
}
