package net.ryuya_dev.protectedlinux.backup;

import static org.junit.Assert.*;

import java.io.*;
import java.nio.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;
import org.junit.*;
import org.junit.rules.TemporaryFolder;

/** Real ZIP I/O and directory renames; no Android/root mocks. */
public class BackupTest {
  @Rule public TemporaryFolder temporary = new TemporaryFolder();

  private Path directory(String name) throws IOException {
    return temporary.newFolder(name).toPath();
  }

  private byte[] export(Map<String, Path> roots) throws IOException {
    ByteArrayOutputStream output = new ByteArrayOutputStream();
    BackupArchive.write(roots, 37, "test-build", output, bytes -> {});
    return output.toByteArray();
  }

  private BackupArchive.Restored restore(byte[] bytes, Path stage, String... roots)
      throws IOException {
    return BackupArchive.read(new ByteArrayInputStream(bytes), stage, Set.of(roots), n -> {});
  }

  @Test
  public void compressedBundleRoundTripAndIndividualSelection() throws Exception {
    Path vm = directory("vm"),
        kernels = directory("kernels"),
        target = temporary.getRoot().toPath().resolve("stage");
    byte[] disk = new byte[4 * 1024 * 1024];
    Arrays.fill(disk, 1024, 2048, (byte) 42);
    Files.write(vm.resolve("debian.qcow2"), disk);
    Files.writeString(vm.resolve("日本語.txt"), "保存したデータ");
    Files.write(vm.resolve("empty"), new byte[0]);
    Files.writeString(vm.resolve("trace.log"), "not included");
    Files.writeString(kernels.resolve("Image"), "kernel");
    byte[] bundle = export(Map.of("vm", vm, "kernels", kernels));
    assertTrue(bundle.length < disk.length / 20);
    BackupArchive.Info info = BackupArchive.inspect(new ByteArrayInputStream(bundle));
    assertEquals(List.of("kernels", "vm"), info.roots());
    assertEquals(4, info.files());
    restore(bundle, target, "vm");
    assertArrayEquals(disk, Files.readAllBytes(target.resolve("vm/debian.qcow2")));
    assertEquals("保存したデータ", Files.readString(target.resolve("vm/日本語.txt")));
    assertEquals(0, Files.size(target.resolve("vm/empty")));
    assertFalse(Files.exists(target.resolve("kernels")));
    assertFalse(Files.exists(target.resolve("vm/trace.log")));
    assertArrayEquals(disk, Files.readAllBytes(vm.resolve("debian.qcow2")));
    ByteArrayOutputStream framed = new ByteArrayOutputStream();
    ArchiveTransport.send(new ByteArrayInputStream(bundle), framed);
    framed.write(ArchiveTransport.COMMIT);
    ByteArrayInputStream pipe = new ByteArrayInputStream(framed.toByteArray());
    BackupArchive.read(
        ArchiveTransport.receive(pipe),
        temporary.getRoot().toPath().resolve("framed-stage"),
        Set.of("kernels"),
        bytes -> {});
    ArchiveTransport.requireCommit(pipe);
  }

  @Test
  public void truncatedCorruptAndMissingFooterNeverPromote() throws Exception {
    Path vm = directory("vm");
    Files.writeString(vm.resolve("disk"), "original");
    byte[] good = export(Map.of("vm", vm));
    for (int cut : new int[] {1, 22, good.length / 2}) {
      Path stage = temporary.getRoot().toPath().resolve("cut-" + cut);
      assertThrows(
          IOException.class, () -> restore(Arrays.copyOf(good, good.length - cut), stage, "vm"));
    }
    byte[] corrupt = bundle("data/vm/disk", "changed".getBytes(), false);
    assertThrows(
        IOException.class,
        () -> restore(corrupt, temporary.getRoot().toPath().resolve("corrupt"), "vm"));
    assertEquals("original", Files.readString(vm.resolve("disk")));
  }

  @Test
  public void traversalAbsoluteUnknownAndMalformedPathsAreRejected() throws Exception {
    int index = 0;
    for (String path :
        List.of(
            "data/vm/../../escape",
            "/data/vm/disk",
            "data/unknown/disk",
            "data/vm//disk",
            "data/vm/C:disk",
            "data/vm/..",
            "data/vm/a\\b")) {
      Path stage = temporary.getRoot().toPath().resolve("bad-" + index++);
      assertThrows(
          IOException.class, () -> restore(bundle(path, new byte[] {1}, true), stage, "vm"));
    }
    assertFalse(Files.exists(temporary.getRoot().toPath().resolve("escape")));
  }

  @Test
  public void exportRejectsSymlinkAndMutableHardLink() throws Exception {
    Path vm = directory("vm"), outside = temporary.newFile("secret").toPath();
    Files.createSymbolicLink(vm.resolve("link"), outside);
    assertThrows(IOException.class, () -> export(Map.of("vm", vm)));
    Files.delete(vm.resolve("link"));
    Files.createLink(vm.resolve("disk"), outside);
    assertThrows(IOException.class, () -> export(Map.of("vm", vm)));
  }

  @Test
  public void intentionalImmutableBaseHardLinksBecomeSeparateFiles() throws Exception {
    Path vm = directory("vm"), mainline = directory("mainline");
    Path base = vm.resolve("base-root.ext4");
    Files.writeString(base, "base");
    Files.setPosixFilePermissions(
        base, java.nio.file.attribute.PosixFilePermissions.fromString("r--------"));
    Files.createLink(mainline.resolve("base-root.ext4"), base);
    Path stage = temporary.getRoot().toPath().resolve("stage");
    restore(export(Map.of("vm", vm, "vm-mainline", mainline)), stage, "vm", "vm-mainline");
    assertFalse(
        Files.isSameFile(
            stage.resolve("vm/base-root.ext4"), stage.resolve("vm-mainline/base-root.ext4")));
  }

  @Test
  public void selectionAndCancellationAreValidated() throws Exception {
    Path vm = directory("vm");
    Files.writeString(vm.resolve("disk"), "data");
    byte[] data = export(Map.of("vm", vm));
    assertThrows(
        IOException.class, () -> restore(data, temporary.getRoot().toPath().resolve("empty")));
    assertThrows(
        IOException.class,
        () -> restore(data, temporary.getRoot().toPath().resolve("other"), "kernels"));
    Thread.currentThread().interrupt();
    try {
      assertThrows(
          InterruptedIOException.class,
          () -> restore(data, temporary.getRoot().toPath().resolve("cancel"), "vm"));
    } finally {
      Thread.interrupted();
    }
  }

  @Test
  public void sourceMutationAbortsExport() throws Exception {
    Path vm = directory("vm"), disk = vm.resolve("disk");
    Files.write(disk, new byte[131072]);
    assertThrows(
        IOException.class,
        () ->
            BackupArchive.write(
                Map.of("vm", vm),
                37,
                "test",
                new ByteArrayOutputStream(),
                bytes -> {
                  try {
                    if (bytes == 65536) Files.write(disk, new byte[4]);
                  } catch (IOException error) {
                    throw new UncheckedIOException(error);
                  }
                }));
  }

  @Test
  public void successfulRestoreRetainsPreviousAndLeavesUnselectedAlone() throws Exception {
    Path root = directory("live"), stage = directory("stage");
    Files.createDirectory(root.resolve("vm"));
    Files.writeString(root.resolve("vm/disk"), "old");
    Files.createDirectory(root.resolve("kernels"));
    Files.writeString(root.resolve("kernels/Image"), "keep");
    Files.createDirectory(stage.resolve("vm"));
    Files.writeString(stage.resolve("vm/disk"), "new");
    new BackupTransaction(root).promote(stage, Set.of("vm"));
    assertEquals("new", Files.readString(root.resolve("vm/disk")));
    assertEquals("keep", Files.readString(root.resolve("kernels/Image")));
    try (var folders = Files.list(root.resolve("backup-recovery"))) {
      Path previous = folders.findFirst().orElseThrow();
      assertEquals("old", Files.readString(previous.resolve("vm/disk")));
    }
    assertFalse(Files.exists(root.resolve("vm-owner.lock.restore-pending")));
  }

  @Test
  public void interruptedPromotionRollsBackEveryRenameBoundary() throws Exception {
    for (int boundary = 0; boundary < 4; boundary++) {
      Path root = directory("boundary-" + boundary);
      Path saved = pending(root, Map.of("vm", "present", "kernels", "absent"));
      Files.createDirectory(root.resolve("vm"));
      Files.writeString(root.resolve("vm/disk"), "old");
      if (boundary >= 1) Files.move(root.resolve("vm"), saved.resolve("vm"));
      if (boundary >= 2) {
        Files.createDirectory(root.resolve("vm"));
        Files.writeString(root.resolve("vm/disk"), "new");
      }
      if (boundary >= 3) {
        Files.createDirectory(root.resolve("kernels"));
        Files.writeString(root.resolve("kernels/Image"), "new-kernel");
      }
      BackupTransaction transaction = new BackupTransaction(root);
      assertTrue(transaction.recover());
      assertFalse(transaction.recover());
      assertEquals("old", Files.readString(root.resolve("vm/disk")));
      assertFalse(Files.exists(root.resolve("kernels")));
    }
  }

  @Test
  public void malformedRecoveryJournalCannotMoveOtherRoots() throws Exception {
    Path root = directory("bad-journal");
    pending(root, Map.of("vm", "absent", "unknown", "present"));
    Files.createDirectory(root.resolve("vm"));
    Files.writeString(root.resolve("vm/disk"), "untouched");
    assertThrows(IOException.class, () -> new BackupTransaction(root).recover());
    assertEquals("untouched", Files.readString(root.resolve("vm/disk")));
    assertTrue(Files.exists(root.resolve("vm-owner.lock.restore-pending")));
  }

  @Test
  public void promotionRefusesSymlinkWithoutTouchingTarget() throws Exception {
    Path root = directory("root"), stage = directory("stage"), target = directory("target");
    Files.writeString(target.resolve("file"), "untouched");
    Files.createSymbolicLink(root.resolve("vm"), target);
    Files.createDirectory(stage.resolve("vm"));
    assertThrows(IOException.class, () -> new BackupTransaction(root).promote(stage, Set.of("vm")));
    assertEquals("untouched", Files.readString(target.resolve("file")));
  }

  @Test
  public void settingsWhitelistAndCaptureDefaults() throws Exception {
    Map<String, Object> values =
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
                    60,
                    "secret",
                    "omit")));
    assertEquals(false, values.get("enabled"));
    assertEquals(false, values.get("microphone_enabled"));
    assertEquals(false, values.get("clipboard_auto"));
    assertEquals(60, values.get("display_fps"));
    assertFalse(values.containsKey("secret"));
    for (String invalid :
        List.of(
            "schema=1\ndisplay_fps=i:999",
            "schema=1\nenabled=true",
            "schema=1\nunknown=b:true",
            "schema=3"))
      assertThrows(IOException.class, () -> PreferenceSnapshot.decode(invalid.getBytes()));
  }

  @Test
  public void restoredDebianIdentityUsesNewInodeAndFixedBackingOnly() throws Exception {
    Path root = directory("root"),
        stage = directory("stage"),
        vm = Files.createDirectory(stage.resolve("vm"));
    Path base = vm.resolve("base-root.ext4");
    try (RandomAccessFile file = new RandomAccessFile(base.toFile(), "rw")) {
      file.setLength(128L * 1024 * 1024);
    }
    Files.write(
        vm.resolve("debian-v2.qcow2"),
        qcow("/data/user/0/net.ryuya_dev.protectedlinux/files/vm/base-root.ext4"));
    DebianRestore.prepare(stage, root, Set.of("vm"), path -> "PLB_BASE_2 new-identity\n");
    assertEquals("PLB_BASE_2 new-identity\n", Files.readString(vm.resolve("base-v2.identity")));
    assertFalse(
        Files.getPosixFilePermissions(base).stream()
            .anyMatch(permission -> permission.name().endsWith("WRITE")));
    Files.write(vm.resolve("debian-v2.qcow2"), qcow("/etc/forbidden"));
    assertThrows(
        IOException.class,
        () -> DebianRestore.prepare(stage, root, Set.of("vm"), path -> "unused"));
  }

  @Test
  public void partialMainlineRestoreRequiresMatchingExternalBase() throws Exception {
    Path root = directory("root"),
        stage = directory("stage"),
        vm = Files.createDirectory(root.resolve("vm")),
        mainline = Files.createDirectory(stage.resolve("vm-mainline"));
    for (Path base : List.of(vm.resolve("base-root.ext4"), mainline.resolve("base-root.ext4")))
      try (RandomAccessFile file = new RandomAccessFile(base.toFile(), "rw")) {
        file.setLength(128L * 1024 * 1024);
      }
    Files.write(
        mainline.resolve("debian-v2.qcow2"),
        qcow("/data/user/0/net.ryuya_dev.protectedlinux/files/vm/base-root.ext4"));
    try (RandomAccessFile file =
        new RandomAccessFile(vm.resolve("base-root.ext4").toFile(), "rw")) {
      file.write(1);
    }
    assertThrows(
        IOException.class,
        () -> DebianRestore.prepare(stage, root, Set.of("vm-mainline"), path -> "unused"));
    assertFalse(Files.exists(mainline.resolve("base-v2.identity")));
  }

  @Test
  public void restoringOneBaseCannotBreakAnUnselectedDependentProfile() throws Exception {
    Path root = directory("root"), stage = directory("stage");
    Path oldVm = Files.createDirectory(root.resolve("vm")),
        newVm = Files.createDirectory(stage.resolve("vm"));
    Path dependent = Files.createDirectory(root.resolve("vm-mainline"));
    for (Path base : List.of(oldVm.resolve("base-root.ext4"), newVm.resolve("base-root.ext4")))
      try (RandomAccessFile file = new RandomAccessFile(base.toFile(), "rw")) {
        file.setLength(128L * 1024 * 1024);
      }
    Files.createLink(dependent.resolve("base-root.ext4"), oldVm.resolve("base-root.ext4"));
    Files.write(
        dependent.resolve("debian-v2.qcow2"),
        qcow("/data/user/0/net.ryuya_dev.protectedlinux/files/vm/base-root.ext4"));
    try (RandomAccessFile file =
        new RandomAccessFile(newVm.resolve("base-root.ext4").toFile(), "rw")) {
      file.write(1);
    }
    assertThrows(
        IOException.class,
        () -> DebianRestore.prepare(stage, root, Set.of("vm"), path -> "unused"));
    assertFalse(Files.exists(newVm.resolve("base-v2.identity")));
    try (InputStream input = Files.newInputStream(oldVm.resolve("base-root.ext4"))) {
      assertEquals(0, input.read());
    }
  }

  private byte[] qcow(String backing) {
    ByteBuffer bytes = ByteBuffer.allocate(4096).order(ByteOrder.BIG_ENDIAN);
    bytes.putInt(0x514649fb).putInt(3).putLong(104).putInt(backing.length()).putInt(12);
    bytes.position(104);
    bytes.put(backing.getBytes(java.nio.charset.StandardCharsets.UTF_8));
    return bytes.array();
  }

  private Path pending(Path root, Map<String, String> values) throws IOException {
    String id = UUID.randomUUID().toString();
    Path saved = Files.createDirectories(root.resolve("backup-recovery").resolve(id));
    Properties properties = new Properties();
    properties.setProperty("schema", "1");
    properties.setProperty("id", id);
    properties.putAll(values);
    try (OutputStream output =
        Files.newOutputStream(root.resolve("vm-owner.lock.restore-pending"))) {
      properties.store(output, "test interrupted journal");
    }
    return saved;
  }

  private byte[] bundle(String path, byte[] data, boolean correctHash) throws Exception {
    ByteArrayOutputStream result = new ByteArrayOutputStream();
    try (ZipOutputStream zip = new ZipOutputStream(result)) {
      zip.putNextEntry(new ZipEntry("metadata.bin"));
      DataOutputStream output = new DataOutputStream(zip);
      output.writeInt(0x504c4231);
      output.writeInt(1);
      output.writeInt(37);
      output.writeUTF("test");
      output.writeLong(0);
      output.writeLong(data.length);
      output.writeInt(1);
      output.writeInt(1);
      output.writeUTF("vm");
      zip.closeEntry();
      zip.putNextEntry(new ZipEntry(path));
      zip.write(data);
      zip.closeEntry();
      zip.putNextEntry(new ZipEntry("checksums.bin"));
      output.writeInt(0x504c4231);
      output.writeInt(1);
      output.writeUTF(path);
      output.writeLong(data.length);
      output.write(correctHash ? MessageDigest.getInstance("SHA-256").digest(data) : new byte[32]);
      zip.closeEntry();
    }
    return result.toByteArray();
  }
}
