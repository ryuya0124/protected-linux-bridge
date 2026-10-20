package net.ryuya_dev.protectedlinux.backup;

import java.io.*;
import java.nio.channels.FileChannel;
import java.nio.file.*;
import java.nio.file.attribute.*;
import java.security.*;
import java.util.*;
import java.util.function.LongConsumer;
import java.util.zip.*;

/** Portable streaming ZIP bundle; no root, commands or Android types in the codec. */
public final class BackupArchive {
  public static final Set<String> ROOTS =
      Set.of("vm", "vm-mainline", "debian-images", "android-guests", "kernels", "settings");
  public static final long MAX_BYTES = 512L * 1024 * 1024 * 1024;
  private static final int MAX_FILES = 20000, MAGIC = 0x504c4231;

  public record Info(
      int sdk, String build, long created, List<String> roots, long bytes, int files) {}

  private record Receipt(long size, byte[] hash) {}

  private record Source(Path path, BasicFileAttributes before) {}

  public record Restored(Info info, Path stage, Set<String> selected) {}

  private BackupArchive() {}

  private static boolean ephemeral(Path file) {
    String name = file.getFileName().toString();
    return name.endsWith(".log")
        || name.endsWith(".pid")
        || name.endsWith(".sock")
        || name.endsWith(".lock");
  }

  private static void checkName(String name) throws IOException {
    if (name.length() > 2048
        || !name.startsWith("data/")
        || name.indexOf('\\') >= 0
        || name.indexOf('\0') >= 0
        || name.indexOf(':') >= 0) throw new IOException("ARCHIVE_PATH");
    String[] parts = name.split("/", -1);
    if (parts.length < 3 || parts.length > 18 || !ROOTS.contains(parts[1]))
      throw new IOException("ARCHIVE_PATH");
    for (String part : parts)
      if (part.isEmpty() || part.equals(".") || part.equals(".."))
        throw new IOException("ARCHIVE_PATH");
  }

  private static MessageDigest digest() {
    try {
      return MessageDigest.getInstance("SHA-256");
    } catch (GeneralSecurityException impossible) {
      throw new AssertionError(impossible);
    }
  }

  private static void privateDirectory(Path directory) throws IOException {
    Files.createDirectories(directory);
    Files.setPosixFilePermissions(directory, PosixFilePermissions.fromString("rwx------"));
  }

  public static Info write(
      Map<String, Path> roots, int sdk, String build, OutputStream output, LongConsumer progress)
      throws IOException {
    if (sdk < 1 || sdk > 1000 || build == null || build.length() > 256)
      throw new IOException("ARCHIVE_FORMAT");
    TreeMap<String, Source> sources = new TreeMap<>();
    TreeSet<String> present = new TreeSet<>();
    long[] total = {0};
    for (var root : roots.entrySet()) {
      if (!ROOTS.contains(root.getKey())) throw new IOException("ARCHIVE_ROOT");
      Path directory = root.getValue();
      if (!Files.exists(directory, LinkOption.NOFOLLOW_LINKS)) continue;
      if (!Files.isDirectory(directory, LinkOption.NOFOLLOW_LINKS))
        throw new IOException("ARCHIVE_TYPE");
      Files.walkFileTree(
          directory,
          EnumSet.noneOf(FileVisitOption.class),
          16,
          new SimpleFileVisitor<>() {
            @Override
            public FileVisitResult visitFile(Path file, BasicFileAttributes attrs)
                throws IOException {
              if (attrs.isSymbolicLink()) throw new IOException("ARCHIVE_LINK");
              if (!attrs.isRegularFile()) {
                if (ephemeral(file)) return FileVisitResult.CONTINUE;
                throw new IOException("ARCHIVE_TYPE");
              }
              if (ephemeral(file)) return FileVisitResult.CONTINUE;
              if (file.getFileName().toString().endsWith(".pending"))
                throw new IOException("ARCHIVE_PENDING");
              Object links = Files.getAttribute(file, "unix:nlink", LinkOption.NOFOLLOW_LINKS);
              // Mainline and stock Debian intentionally hard-link their immutable base.
              // Serialize separate regular files, never restore a hard link.
              boolean immutableBase =
                  file.getFileName().toString().equals("base-root.ext4")
                      && !Files.getPosixFilePermissions(file, LinkOption.NOFOLLOW_LINKS).stream()
                          .anyMatch(permission -> permission.name().endsWith("WRITE"));
              if (!(links instanceof Number)
                  || ((Number) links).longValue() < 1
                  || ((Number) links).longValue() != 1 && !immutableBase)
                throw new IOException("ARCHIVE_LINK");
              String name =
                  "data/"
                      + root.getKey()
                      + "/"
                      + directory.relativize(file).toString().replace(File.separatorChar, '/');
              checkName(name);
              if (sources.size() >= MAX_FILES
                  || attrs.size() < 0
                  || attrs.size() > MAX_BYTES - total[0]) throw new IOException("ARCHIVE_LIMIT");
              total[0] += attrs.size();
              sources.put(name, new Source(file, attrs));
              present.add(root.getKey());
              return FileVisitResult.CONTINUE;
            }
          });
    }
    if (sources.isEmpty()) throw new IOException("ARCHIVE_EMPTY");
    Info info =
        new Info(
            sdk, build, System.currentTimeMillis(), List.copyOf(present), total[0], sources.size());
    Map<String, Receipt> receipts = new TreeMap<>();
    try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(output, 65536))) {
      zip.setLevel(Deflater.BEST_SPEED);
      entry(zip, "metadata.bin");
      DataOutputStream metadata = new DataOutputStream(zip);
      metadata.writeInt(MAGIC);
      metadata.writeInt(1);
      metadata.writeInt(info.sdk());
      metadata.writeUTF(info.build());
      metadata.writeLong(info.created());
      metadata.writeLong(info.bytes());
      metadata.writeInt(info.files());
      metadata.writeInt(info.roots().size());
      for (String root : info.roots()) metadata.writeUTF(root);
      zip.closeEntry();
      byte[] buffer = new byte[65536];
      long copied = 0;
      for (var item : sources.entrySet()) {
        if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
        entry(zip, item.getKey());
        MessageDigest hash = digest();
        long length = 0;
        try (InputStream input =
            Files.newInputStream(item.getValue().path(), LinkOption.NOFOLLOW_LINKS)) {
          int count;
          while ((count = input.read(buffer)) != -1) {
            if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
            length += count;
            if (length > item.getValue().before().size()) throw new IOException("ARCHIVE_CHANGED");
            hash.update(buffer, 0, count);
            zip.write(buffer, 0, count);
            copied += count;
            progress.accept(copied);
          }
        }
        BasicFileAttributes after =
            Files.readAttributes(
                item.getValue().path(), BasicFileAttributes.class, LinkOption.NOFOLLOW_LINKS);
        BasicFileAttributes before = item.getValue().before();
        if (length != before.size()
            || after.size() != before.size()
            || !Objects.equals(after.fileKey(), before.fileKey())
            || !after.lastModifiedTime().equals(before.lastModifiedTime()))
          throw new IOException("ARCHIVE_CHANGED");
        receipts.put(item.getKey(), new Receipt(length, hash.digest()));
        zip.closeEntry();
      }
      entry(zip, "checksums.bin");
      DataOutputStream manifest = new DataOutputStream(zip);
      manifest.writeInt(MAGIC);
      manifest.writeInt(receipts.size());
      for (var item : receipts.entrySet()) {
        manifest.writeUTF(item.getKey());
        manifest.writeLong(item.getValue().size());
        manifest.write(item.getValue().hash());
      }
      zip.closeEntry();
    }
    return info;
  }

  private static void entry(ZipOutputStream zip, String name) throws IOException {
    ZipEntry entry = new ZipEntry(name);
    entry.setTime(0);
    zip.putNextEntry(entry);
  }

  private static Info metadata(ZipInputStream zip) throws IOException {
    ZipEntry entry = zip.getNextEntry();
    if (entry == null || !entry.getName().equals("metadata.bin"))
      throw new IOException("ARCHIVE_FORMAT");
    byte[] bytes = zip.readNBytes(65537);
    if (bytes.length > 65536) throw new IOException("ARCHIVE_LIMIT");
    DataInputStream input = new DataInputStream(new ByteArrayInputStream(bytes));
    if (input.readInt() != MAGIC || input.readInt() != 1) throw new IOException("ARCHIVE_VERSION");
    int sdk = input.readInt();
    String build = input.readUTF();
    long created = input.readLong(), size = input.readLong();
    int files = input.readInt(), count = input.readInt();
    if (sdk < 1
        || sdk > 1000
        || build.length() > 256
        || created < 0
        || files < 1
        || files > MAX_FILES
        || size < 0
        || size > MAX_BYTES
        || count < 1
        || count > ROOTS.size()) throw new IOException("ARCHIVE_LIMIT");
    Set<String> roots = new LinkedHashSet<>();
    for (int i = 0; i < count; i++) {
      String root = input.readUTF();
      if (!ROOTS.contains(root) || !roots.add(root)) throw new IOException("ARCHIVE_ROOT");
    }
    if (input.read() != -1) throw new IOException("ARCHIVE_FORMAT");
    return new Info(sdk, build, created, List.copyOf(roots), size, files);
  }

  public static Info inspect(InputStream input) throws IOException {
    try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(input))) {
      return metadata(zip);
    }
  }

  public static Restored read(
      InputStream source, Path stage, Set<String> selected, LongConsumer progress)
      throws IOException {
    if (selected.isEmpty()
        || !ROOTS.containsAll(selected)
        || Files.exists(stage, LinkOption.NOFOLLOW_LINKS))
      throw new IOException("ARCHIVE_SELECTION");
    privateDirectory(stage);
    TailInput input = new TailInput(new BufferedInputStream(source, 65536));
    Map<String, Receipt> observed = new TreeMap<>();
    try (ZipInputStream zip = new ZipInputStream(input)) {
      Info info = metadata(zip);
      if (!info.roots().containsAll(selected)) throw new IOException("ARCHIVE_SELECTION");
      for (String root : selected) privateDirectory(stage.resolve(root));
      FileStore store = Files.getFileStore(stage);
      long nextSpaceCheck = 0, written = 0;
      byte[] buffer = new byte[65536];
      long total = 0;
      boolean verified = false;
      ZipEntry entry;
      while ((entry = zip.getNextEntry()) != null) {
        if (entry.getName().equals("checksums.bin")) {
          DataInputStream hashes = new DataInputStream(zip);
          if (hashes.readInt() != MAGIC
              || hashes.readInt() != observed.size()
              || observed.size() != info.files()) throw new IOException("ARCHIVE_MANIFEST");
          for (var item : observed.entrySet()) {
            if (!hashes.readUTF().equals(item.getKey())
                || hashes.readLong() != item.getValue().size()
                || !MessageDigest.isEqual(hashes.readNBytes(32), item.getValue().hash()))
              throw new IOException("ARCHIVE_CHECKSUM");
          }
          if (hashes.read() != -1 || zip.getNextEntry() != null || total != info.bytes())
            throw new IOException("ARCHIVE_MANIFEST");
          verified = true;
          break;
        }
        String name = entry.getName();
        checkName(name);
        String root = name.split("/", 3)[1];
        if (!info.roots().contains(root)
            || observed.containsKey(name)
            || observed.size() >= info.files()
            || entry.isDirectory()) throw new IOException("ARCHIVE_ENTRY");
        Path target = selected.contains(root) ? stage.resolve(name.substring(5)) : null;
        FileChannel file = null;
        if (target != null) {
          privateDirectory(target.getParent());
          file =
              FileChannel.open(
                  target,
                  StandardOpenOption.CREATE_NEW,
                  StandardOpenOption.WRITE,
                  LinkOption.NOFOLLOW_LINKS);
          Files.setPosixFilePermissions(target, PosixFilePermissions.fromString("rw-------"));
        }
        MessageDigest hash = digest();
        long length = 0;
        try {
          int count;
          while ((count = zip.read(buffer)) != -1) {
            if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
            if (count > MAX_BYTES - total || count > info.bytes() - total)
              throw new IOException("ARCHIVE_LIMIT");
            hash.update(buffer, 0, count);
            length += count;
            total += count;
            if (file != null) {
              boolean zero = true;
              for (int i = 0; i < count; i++)
                if (buffer[i] != 0) {
                  zero = false;
                  break;
                }
              if (zero) file.position(file.position() + count);
              else {
                if (written >= nextSpaceCheck) {
                  if (store.getUsableSpace() < 512L * 1024 * 1024)
                    throw new IOException("ARCHIVE_SPACE");
                  nextSpaceCheck = written + 64L * 1024 * 1024;
                }
                written += count;
                java.nio.ByteBuffer bytes = java.nio.ByteBuffer.wrap(buffer, 0, count);
                while (bytes.hasRemaining()) file.write(bytes);
              }
            }
            progress.accept(total);
          }
          if (file != null) {
            if (length > 0 && file.size() < length) {
              file.position(length - 1);
              file.write(java.nio.ByteBuffer.wrap(new byte[1]));
            }
            file.force(true);
          }
        } finally {
          if (file != null) file.close();
        }
        observed.put(name, new Receipt(length, hash.digest()));
      }
      if (!verified) throw new IOException("ARCHIVE_INCOMPLETE");
      long footerBytes = 0;
      int footerCount;
      while ((footerCount = input.read(buffer)) != -1) {
        footerBytes += footerCount;
        if (footerBytes > MAX_FILES * 4096L) throw new IOException("ARCHIVE_LIMIT");
      }
      if (!input.complete(observed.size() + 2)) throw new IOException("ARCHIVE_FOOTER");

      // File contents and all descendant directory entries must precede promotion.
      try (var directories = Files.walk(stage)) {
        for (Path directory :
            directories
                .filter(path -> Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS))
                .sorted(Comparator.reverseOrder())
                .toList()) {
          try (FileChannel channel = FileChannel.open(directory, StandardOpenOption.READ)) {
            channel.force(true);
          }
        }
      }
      return new Restored(info, stage, Set.copyOf(selected));
    }
  }

  private static final class TailInput extends FilterInputStream {
    private final byte[] tail = new byte[22];
    private long count;

    TailInput(InputStream source) {
      super(source);
    }

    @Override
    public int read() throws IOException {
      int value = in.read();
      if (value >= 0) tail[(int) (count++ % 22)] = (byte) value;
      return value;
    }

    @Override
    public int read(byte[] data, int offset, int length) throws IOException {
      int result = in.read(data, offset, length);
      if (result > 0)
        for (int i = 0; i < result; i++) tail[(int) (count++ % 22)] = data[offset + i];
      return result;
    }

    boolean complete(int entries) {
      if (count < 22) return false;
      byte[] end = new byte[22];
      for (int i = 0; i < 22; i++) end[i] = tail[(int) ((count + i) % 22)];
      return end[0] == 'P'
          && end[1] == 'K'
          && end[2] == 5
          && end[3] == 6
          && ((end[10] & 255) | ((end[11] & 255) << 8)) == entries
          && end[20] == 0
          && end[21] == 0;
    }
  }
}
