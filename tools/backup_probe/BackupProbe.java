package net.ryuya_dev.protectedlinux.backupprobe;

import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import net.ryuya_dev.protectedlinux.backup.*;
import net.ryuya_dev.protectedlinux.backuproot.BackupRoot;

/** Small real-Android codec test. Never reads or promotes a real VM profile. */
public final class BackupProbe {
  private static final Path ROOT = Path.of("/data/user/0/net.ryuya_dev.protectedlinux/files");

  private static void check(boolean condition, String name) throws IOException {
    if (!condition) throw new IOException("PROBE_FAILED " + name);
    System.out.println("PASS " + name);
  }

  public static void main(String[] args) throws Exception {
    if (android.os.Process.myUid() != 0 || args.length != 2)
      throw new IOException("PROBE_ARGUMENT");
    Path job = Path.of(args[0]);
    if (!job.getParent().equals(ROOT.resolve("backup-jobs"))
        || !job.getFileName().toString().matches("probe-[a-f0-9]{32}")
        || !Files.isDirectory(job, LinkOption.NOFOLLOW_LINKS)) throw new IOException("PROBE_PATH");
    Path library = job.resolve("libbackup-lock.so");
    byte[] digest = MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(library));
    if (!HexFormat.of().formatHex(digest).equals(args[1])) throw new IOException("PROBE_LIBRARY");
    // The production JNI binding acquires both locks. Busy means refuse, never stop a VM.
    System.load(library.toString());
    int owner = BackupRoot.lock();
    if (owner < 0) throw new IOException("ARCHIVE_BUSY");
    try {
      if (Files.exists(ROOT.resolve("vm-owner.lock.restore-pending"), LinkOption.NOFOLLOW_LINKS))
        throw new IOException("RESTORE_PENDING");
      Path source = Files.createDirectory(job.resolve("source"));
      Path vm = Files.createDirectory(source.resolve("vm"));
      Path kernels = Files.createDirectory(source.resolve("kernels"));
      byte[] payload = new byte[2 * 1024 * 1024];
      new Random(73).nextBytes(payload);
      Arrays.fill(payload, 131072, payload.length, (byte) 0);
      Files.write(vm.resolve("synthetic-disk.bin"), payload, StandardOpenOption.CREATE_NEW);
      Files.writeString(kernels.resolve("synthetic-image"), "guest-only test");
      Files.writeString(vm.resolve("session.log"), "must not enter bundle");
      ByteArrayOutputStream archive = new ByteArrayOutputStream();
      BackupArchive.write(
          Map.of("vm", vm, "kernels", kernels), 37, "synthetic-device-probe", archive, n -> {});
      byte[] zip = archive.toByteArray();
      check(zip.length < payload.length / 2, "DEFLATE_COMPRESSION");
      ByteArrayOutputStream framed = new ByteArrayOutputStream();
      ArchiveTransport.send(new ByteArrayInputStream(zip), framed);
      framed.write(ArchiveTransport.COMMIT);
      System.out.println("STEP FRAMED_INPUT_READY");
      ByteArrayInputStream input = new ByteArrayInputStream(framed.toByteArray());
      Path stage = job.resolve("stage");
      System.out.println("STEP SELECTIVE_EXTRACTION");
      System.out.println("STEP PATH_FREE_SPACE_QUERY");
      long available = job.toFile().getUsableSpace();
      check(available > 512L * 1024 * 1024, "DEVICE_STORAGE_HEADROOM");
      BackupArchive.read(ArchiveTransport.receive(input), stage, Set.of("vm"), n -> {});
      ArchiveTransport.requireCommit(input);
      check(
          Arrays.equals(payload, Files.readAllBytes(stage.resolve("vm/synthetic-disk.bin"))),
          "FULL_CONTENT_READBACK");
      check(!Files.exists(stage.resolve("kernels")), "UNSELECTED_ROOT_NOT_EXTRACTED");
      check(!Files.exists(stage.resolve("vm/session.log")), "EPHEMERAL_EXCLUDED");
      // This separate synthetic root is the only target passed to BackupTransaction.
      Path target = Files.createDirectory(job.resolve("target"));
      Files.createDirectory(target.resolve("vm"));
      Files.writeString(target.resolve("vm/original"), "old synthetic data");
      Files.createDirectory(target.resolve("kernels"));
      Files.writeString(target.resolve("kernels/untouched"), "not selected");
      BackupTransaction transaction = new BackupTransaction(target);
      transaction.promote(stage, Set.of("vm"));
      check(
          Arrays.equals(payload, Files.readAllBytes(target.resolve("vm/synthetic-disk.bin"))),
          "ATOMIC_PROMOTION_AND_FSYNC");
      try (var copies = Files.list(target.resolve("backup-recovery"))) {
        Path copy = copies.findFirst().orElseThrow();
        check(
            Files.readString(copy.resolve("vm/original")).equals("old synthetic data"),
            "PRIOR_COPY_RETAINED");
      }
      check(
          Files.readString(target.resolve("kernels/untouched")).equals("not selected"),
          "UNSELECTED_TARGET_UNCHANGED");
      check(!transaction.recover(), "NO_PENDING_TRANSACTION");
      Map<String, Object> preferences =
          PreferenceSnapshot.decode(
              PreferenceSnapshot.encode(
                  Map.of(
                      "enabled",
                      true,
                      "microphone_enabled",
                      true,
                      "clipboard_auto",
                      true,
                      "display_fps",
                      60)));
      check(
          Boolean.FALSE.equals(preferences.get("enabled"))
              && Boolean.FALSE.equals(preferences.get("microphone_enabled"))
              && Boolean.FALSE.equals(preferences.get("clipboard_auto")),
          "RESTORED_CAPTURE_AND_CONNECTION_DISABLED");
      try {
        BackupArchive.read(
            new ByteArrayInputStream(Arrays.copyOf(zip, zip.length - 12)),
            job.resolve("truncated"),
            Set.of("vm"),
            n -> {});
        throw new IOException("PROBE_FAILED TRUNCATED_ARCHIVE_ACCEPTED");
      } catch (IOException expected) {
        if (expected.getMessage() != null && expected.getMessage().startsWith("PROBE_FAILED"))
          throw expected;
        System.out.println("PASS TRUNCATED_ARCHIVE_REJECTED");
      }
      System.out.println("BACKUP_ANDROID_PROBE_OK bytes=" + payload.length);
    } finally {
      BackupRoot.unlock(owner);
    }
  }
}
