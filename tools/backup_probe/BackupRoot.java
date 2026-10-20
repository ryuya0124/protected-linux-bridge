package net.ryuya_dev.protectedlinux.backuproot;

/** Developer-only JNI binding to the production owner/maintenance flock library. */
public final class BackupRoot {
  private BackupRoot() {}

  public static native int lock();

  public static native void unlock(int fd);
}
