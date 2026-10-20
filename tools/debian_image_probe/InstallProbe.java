import java.nio.file.*;
import net.ryuya_dev.protectedlinux.debian.DebianImage;

/** Host-only, no VM. Feed the official archive on stdin; the temporary image is removed. */
public final class InstallProbe {
  public static void main(String[] args) throws Exception {
    if (args.length != 1) throw new IllegalArgumentException("fresh output directory required");
    Path directory = Path.of(args[0]);
    Files.createDirectory(directory);
    Path image = directory.resolve("root_part");
    try {
      DebianImage.install(System.in, image, bytes -> {});
      System.out.println("DEBIAN_IMAGE_VERIFIED bytes=" + Files.size(image));
    } finally {
      Files.deleteIfExists(image);
      Files.delete(directory);
    }
  }
}
