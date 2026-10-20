package net.ryuya_dev.protectedlinux.backup;

import java.io.*;
import java.nio.*;
import java.nio.channels.FileChannel;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.security.*;
import java.util.*;

/** Rebind copied-file identities without weakening the normal VM's immutable-base guard. */
public final class DebianRestore {
  private static final String FIXED_ROOT = "/data/user/0/net.ryuya_dev.protectedlinux/files/";

  public interface Identity {
    String of(Path base) throws IOException;
  }

  private DebianRestore() {}

  public static void prepare(Path stage, Path live, Set<String> selected, Identity identity)
      throws IOException {
    Map<Path, byte[]> hashes = new HashMap<>();
    // A selected base may also be referenced by a profile the user did not select.
    // Refuse a content change rather than silently breaking that unselected disk.
    for (String consumer : List.of("vm", "vm-mainline")) {
      Path existing = live.resolve(consumer);
      if (selected.contains(consumer) || !Files.exists(existing, LinkOption.NOFOLLOW_LINKS))
        continue;
      if (!Files.isDirectory(existing, LinkOption.NOFOLLOW_LINKS))
        throw new IOException("RESTORE_DEPENDENCY");
      try (var files = Files.list(existing)) {
        for (Path file :
            files.filter(path -> path.getFileName().toString().endsWith(".qcow2")).toList()) {
          String reference = backing(file);
          if (reference == null) continue;
          String owner = knownOwner(reference);
          if (!selected.contains(owner)) continue;
          Path original = live.resolve(owner).resolve("base-root.ext4");
          Path replacement = stage.resolve(owner).resolve("base-root.ext4");
          requireBase(original);
          requireBase(replacement);
          if (!MessageDigest.isEqual(cachedHash(original, hashes), cachedHash(replacement, hashes)))
            throw new IOException("RESTORE_DEPENDENCY");
        }
      }
    }
    for (String group : List.of("vm", "vm-mainline")) {
      if (!selected.contains(group)) continue;
      Path directory = stage.resolve(group), base = directory.resolve("base-root.ext4");
      boolean hasBase = Files.exists(base, LinkOption.NOFOLLOW_LINKS);
      if (!hasBase
          && !Files.exists(directory.resolve("debian-v2.qcow2"), LinkOption.NOFOLLOW_LINKS))
        continue;
      requireBase(base);
      Files.setPosixFilePermissions(base, PosixFilePermissions.fromString("r--------"));
      try (var files = Files.list(directory)) {
        for (Path file :
            files.filter(path -> path.getFileName().toString().endsWith(".qcow2")).toList()) {
          String dependency = backing(file);
          if (dependency == null) continue; // Standalone/compacted qcow2 has no external backing.
          String owner = knownOwner(dependency);
          if (owner.isEmpty()) throw new IOException("RESTORE_DEPENDENCY");
          Path referred =
              (selected.contains(owner) ? stage : live).resolve(owner).resolve("base-root.ext4");
          requireBase(referred);
          if (!base.equals(referred)
              && !MessageDigest.isEqual(cachedHash(base, hashes), cachedHash(referred, hashes)))
            throw new IOException("RESTORE_DEPENDENCY");
        }
      }
      // Staging and final root are on one filesystem; rename retains this exact inode/mtime.
      Path marker = directory.resolve("base-v2.identity");
      Files.deleteIfExists(marker);
      Files.writeString(
          marker, identity.of(base), StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE);
      Files.setPosixFilePermissions(marker, PosixFilePermissions.fromString("rw-------"));
      try (FileChannel channel = FileChannel.open(marker, StandardOpenOption.WRITE)) {
        channel.force(true);
      }
      try (FileChannel channel = FileChannel.open(directory, StandardOpenOption.READ)) {
        channel.force(true);
      }
    }
  }

  private static String knownOwner(String reference) {
    if (reference.equals(FIXED_ROOT + "vm/base-root.ext4")) return "vm";
    if (reference.equals(FIXED_ROOT + "vm-mainline/base-root.ext4")) return "vm-mainline";
    return "";
  }

  private static byte[] cachedHash(Path path, Map<Path, byte[]> hashes) throws IOException {
    byte[] result = hashes.get(path);
    if (result == null) {
      result = hash(path);
      hashes.put(path, result);
    }
    return result;
  }

  private static void requireBase(Path base) throws IOException {
    if (!Files.isRegularFile(base, LinkOption.NOFOLLOW_LINKS)
        || Files.size(base) < 128L * 1024 * 1024
        || Files.size(base) > 8L * 1024 * 1024 * 1024) throw new IOException("RESTORE_DEPENDENCY");
  }

  private static String backing(Path file) throws IOException {
    try (FileChannel channel =
        FileChannel.open(file, StandardOpenOption.READ, LinkOption.NOFOLLOW_LINKS)) {
      ByteBuffer header = ByteBuffer.allocate(72).order(ByteOrder.BIG_ENDIAN);
      while (header.hasRemaining())
        if (channel.read(header) < 0) throw new IOException("RESTORE_QCOW");
      header.flip();
      if (header.getInt() != 0x514649fb || !Set.of(2, 3).contains(header.getInt()))
        throw new IOException("RESTORE_QCOW");
      long offset = header.getLong();
      int length = header.getInt(), bits = header.getInt();
      if (offset == 0 && length == 0) return null;
      if (bits < 9
          || bits > 21
          || offset < 72
          || length < 1
          || length > 4096
          || offset > (1L << bits) - length) throw new IOException("RESTORE_QCOW");
      ByteBuffer name = ByteBuffer.allocate(length);
      channel.position(offset);
      while (name.hasRemaining()) if (channel.read(name) < 0) throw new IOException("RESTORE_QCOW");
      return new String(name.array(), java.nio.charset.StandardCharsets.UTF_8);
    }
  }

  private static byte[] hash(Path file) throws IOException {
    try {
      MessageDigest hash = MessageDigest.getInstance("SHA-256");
      try (InputStream input = Files.newInputStream(file, LinkOption.NOFOLLOW_LINKS)) {
        byte[] buffer = new byte[65536];
        int count;
        while ((count = input.read(buffer)) != -1) {
          if (Thread.currentThread().isInterrupted()) throw new InterruptedIOException();
          hash.update(buffer, 0, count);
        }
      }
      return hash.digest();
    } catch (NoSuchAlgorithmException impossible) {
      throw new AssertionError(impossible);
    }
  }
}
