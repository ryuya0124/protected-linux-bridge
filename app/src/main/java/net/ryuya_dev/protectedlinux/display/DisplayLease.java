package net.ryuya_dev.protectedlinux.display;

/** Identity policy independent of Binder. One root process and publisher per viewer. */
public final class DisplayLease {
  private final String session;
  private int rootPid;
  private Object publisher;
  private boolean closed;

  public DisplayLease(DisplayCapability capability) {
    session = capability.session;
  }

  private boolean matches(int uid, int pid, String token) {
    return !closed && uid == 0 && pid > 1 && session.equals(token);
  }

  public synchronized boolean claim(int uid, int pid, String token) {
    if (!matches(uid, pid, token) || (rootPid != 0 && rootPid != pid)) return false;
    rootPid = pid;
    return true;
  }

  public synchronized boolean mayPublish(int uid, int pid, String token) {
    return matches(uid, pid, token) && rootPid == pid && publisher == null;
  }

  public synchronized boolean publish(int uid, int pid, String token, Object service) {
    if (service == null || !mayPublish(uid, pid, token)) return false;
    publisher = service;
    return true;
  }

  public synchronized boolean revoke(int uid, int pid, String token) {
    if (!matches(uid, pid, token) || rootPid != pid) return false;
    close();
    return true;
  }

  public synchronized boolean died(Object service) {
    if (closed || publisher != service) return false;
    close();
    return true;
  }

  public synchronized boolean open() {
    return !closed;
  }

  public synchronized void close() {
    closed = true;
    publisher = null;
  }
}
