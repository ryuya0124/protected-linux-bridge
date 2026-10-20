package net.ryuya_dev.protectedlinux.backuproot;

import android.os.Build;
import java.io.*;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.*;
import java.util.*;
import net.ryuya_dev.protectedlinux.backup.*;

/** Root worker in the signed APK. Fixed private roots only; never starts or stops a VM. */
public final class BackupRoot {
  private static final Path ROOT = Path.of("/data/user/0/net.ryuya_dev.protectedlinux/files");

  private static native int lock();

  private static native void unlock(int fd);

  private static void awaitCommit() throws IOException {
    System.out.println("PREPARED");
    System.out.flush();
    ArchiveTransport.requireCommit(System.in);
  }

  private static java.util.function.LongConsumer reportProgress() {
    return new java.util.function.LongConsumer() {
      private long next;

      public void accept(long bytes) {
        if (bytes >= next) {
          System.err.println("ARCHIVE_PROGRESS " + bytes);
          next = bytes + 16L * 1024 * 1024;
        }
      }
    };
  }

  private static Path createJob() throws IOException {
    Path jobs = ROOT.resolve("backup-jobs");
    if (Files.exists(jobs, LinkOption.NOFOLLOW_LINKS)
        && !Files.isDirectory(jobs, LinkOption.NOFOLLOW_LINKS))
      throw new IOException("ARCHIVE_TYPE");
    Files.createDirectories(jobs);
    Files.setPosixFilePermissions(jobs, PosixFilePermissions.fromString("rwx------"));
    return Files.createTempDirectory(
        jobs,
        "job-",
        PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rwx------")));
  }

  public static void main(String[] args) {
    int owner = -1;
    Path job = null;
    int result = 1;
    try {
      if (android.os.Process.myUid() != 0 || args.length < 1 || args.length > 2)
        throw new IOException("ARCHIVE_ROOT");
      Path library = ROOT.resolve("runtime/libbackup-lock.so");
      if (!Files.isRegularFile(library, LinkOption.NOFOLLOW_LINKS)
          || Files.size(library) > 1024 * 1024) throw new IOException("ARCHIVE_RUNTIME");
      MessageDigest digest = MessageDigest.getInstance("SHA-256");
      try (InputStream input = Files.newInputStream(library, LinkOption.NOFOLLOW_LINKS)) {
        byte[] bytes = new byte[65536];
        int count;
        while ((count = input.read(bytes)) != -1) digest.update(bytes, 0, count);
      }
      if (!HexFormat.of().formatHex(digest.digest()).equals(BackupBuild.LIBRARY_SHA256))
        throw new IOException("ARCHIVE_RUNTIME");
      System.load(library.toString());
      owner = lock();
      if (owner < 0) {
        result = 28;
        throw new IOException("ARCHIVE_BUSY");
      }
      BackupTransaction transaction = new BackupTransaction(ROOT);
      transaction.recover();
      if (args[0].equals("--recover") && args.length == 1) {
        System.out.println("RECOVERED");
        result = 0;
      } else if (Set.of("--install-debian", "--import-debian").contains(args[0])
          && args.length == 1) {
        Path image = ROOT.resolve("debian-images");
        if (Files.exists(image, LinkOption.NOFOLLOW_LINKS))
          throw new IOException("ARCHIVE_IMAGE_EXISTS");
        System.err.println("ARCHIVE_READY");
        job = createJob();
        Path stage = Files.createDirectory(job.resolve("stage"));
        Path target = Files.createDirectory(stage.resolve("debian-images")).resolve("root_part");
        var progress = reportProgress();
        if (args[0].equals("--install-debian")) {
          net.ryuya_dev.protectedlinux.debian.DebianImage.install(
              ArchiveTransport.receive(System.in), target, progress);
        } else {
          Path source =
              Path.of("/data/user/0/com.android.virtualization.terminal/files/linux/root_part");
          if (!Files.isRegularFile(source, LinkOption.NOFOLLOW_LINKS))
            throw new IOException("ARCHIVE_IMAGE_MISSING");
          try (InputStream input = Files.newInputStream(source, LinkOption.NOFOLLOW_LINKS)) {
            net.ryuya_dev.protectedlinux.debian.DebianImage.importRaw(input, target, progress);
          }
        }
        awaitCommit();
        transaction.promote(stage, Set.of("debian-images"));
        System.out.println("INSTALLED");
        result = 0;
      } else {
        if (args.length != 2) throw new IOException("ARCHIVE_SELECTION");
        Set<String> selected = new TreeSet<>(Arrays.asList(args[1].split(",", -1)));
        if (selected.isEmpty() || !BackupArchive.ROOTS.containsAll(selected))
          throw new IOException("ARCHIVE_SELECTION");
        System.err.println("ARCHIVE_READY");
        job = createJob();
        if (args[0].equals("--export")) {
          Map<String, Path> roots = new TreeMap<>();
          for (String id : selected) roots.put(id, ROOT.resolve(id));
          if (selected.contains("settings")) {
            DataInputStream input = new DataInputStream(System.in);
            int length = input.readInt();
            if (length < 0 || length > 65536) throw new IOException("ARCHIVE_SETTINGS");
            byte[] settings = input.readNBytes(length);
            if (settings.length != length) throw new EOFException();
            Path directory = Files.createDirectory(job.resolve("settings"));
            Files.write(
                directory.resolve("preferences.properties"),
                settings,
                StandardOpenOption.CREATE_NEW);
            roots.put("settings", directory);
          }
          BackupArchive.write(
              roots,
              Build.VERSION.SDK_INT,
              Build.VERSION.INCREMENTAL,
              System.out,
              reportProgress());
        } else if (args[0].equals("--restore")) {
          BackupArchive.Restored restored =
              BackupArchive.read(
                  ArchiveTransport.receive(System.in),
                  job.resolve("stage"),
                  selected,
                  reportProgress());
          byte[] settings = null;
          if (selected.remove("settings")) {
            Path file = restored.stage().resolve("settings/preferences.properties");
            if (!Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS) || Files.size(file) > 65536)
              throw new IOException("ARCHIVE_SETTINGS");
            settings = Files.readAllBytes(file);
            PreferenceSnapshot.decode(settings);
          }
          DebianRestore.prepare(
              restored.stage(),
              ROOT,
              selected,
              base -> {
                try {
                  var stat = android.system.Os.stat(base.toString());
                  return "PLB_BASE_2 "
                      + Long.toUnsignedString(stat.st_dev)
                      + " "
                      + Long.toUnsignedString(stat.st_ino)
                      + " "
                      + stat.st_size
                      + " "
                      + stat.st_mtim.tv_sec
                      + " "
                      + stat.st_mtim.tv_nsec
                      + "\n";
                } catch (android.system.ErrnoException error) {
                  throw new IOException("RESTORE_IDENTITY", error);
                }
              });
          awaitCommit();
          transaction.promote(restored.stage(), selected);
          System.out.println("RESTORED");
          if (settings != null)
            System.out.println("SETTINGS " + Base64.getEncoder().encodeToString(settings));
        } else throw new IOException("ARCHIVE_ACTION");
        result = 0;
      }
    } catch (Throwable failure) {
      String message = failure.getMessage();
      System.err.println(
          "ARCHIVE_ERROR "
              + (message != null && message.matches("(ARCHIVE|RESTORE)_[A-Z_]+")
                  ? message
                  : "ARCHIVE_IO"));
    } finally {
      if (job != null) {
        try (var files = Files.walk(job)) {
          for (Path file : files.sorted(Comparator.reverseOrder()).toList()) Files.delete(file);
        } catch (IOException ignored) {
          /* Incomplete private staging remains identifiable. */
        }
      }
      if (owner >= 0) unlock(owner);
    }
    System.exit(result);
  }
}
