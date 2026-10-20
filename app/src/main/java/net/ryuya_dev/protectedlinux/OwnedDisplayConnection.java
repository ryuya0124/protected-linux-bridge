package net.ryuya_dev.protectedlinux;

import android.os.*;
import android.view.Surface;
import java.io.*;
import java.security.SecureRandom;
import java.util.HexFormat;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import net.ryuya_dev.protectedlinux.display.*;

/** One app-owned display lease. No service-manager lookup and no automatic VM start. */
final class OwnedDisplayConnection implements AutoCloseable {
  private static final AtomicReference<OwnedDisplayConnection> CURRENT = new AtomicReference<>();
  private static final String DESCRIPTOR = "net.ryuya_dev.protectedlinux.display.IOwnedDisplay";
  private final DisplayCapability capability;
  private final DisplayLease lease;
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private final DisplayTimeline<Surface> timeline = new DisplayTimeline<>(Surface::release);
  private final AtomicLong sequence = new AtomicLong();
  private volatile IOwnedDisplay remote;
  private IBinder binder;
  private IBinder.DeathRecipient death;
  private final AtomicBoolean closing = new AtomicBoolean();

  OwnedDisplayConnection() throws IOException {
    byte[] random = new byte[32];
    new SecureRandom().nextBytes(random);
    capability =
        new DisplayCapability(android.os.Process.myUid(), HexFormat.of().formatHex(random));
    lease = new DisplayLease(capability);
    if (!CURRENT.compareAndSet(null, this)) {
      worker.shutdown();
      throw new IOException("Display already owned");
    }
  }

  static OwnedDisplayConnection current() {
    return CURRENT.get();
  }

  void writeCapability(OutputStream pipe) throws IOException {
    if (!lease.open()) throw new IOException("Display closed");
    capability.write(pipe);
  }

  Bundle claim(int uid, int pid, String token) {
    if (!lease.claim(uid, pid, token)) throw new SecurityException("Display claim refused");
    Bundle reply = new Bundle();
    reply.putInt("protocol", 1);
    reply.putInt("viewer_uid", capability.viewerUid);
    return reply;
  }

  void publish(int uid, int pid, String token, IBinder service) throws RemoteException {
    if (!lease.mayPublish(uid, pid, token) || service == null)
      throw new SecurityException("Display publisher refused");
    long identity = Binder.clearCallingIdentity();
    IBinder.DeathRecipient recipient =
        () -> {
          if (lease.died(service)) fail();
        };
    try {
      if (!DESCRIPTOR.equals(service.getInterfaceDescriptor()))
        throw new SecurityException("Display protocol descriptor");
      IOwnedDisplay candidate = IOwnedDisplay.Stub.asInterface(service);
      if (candidate.getProtocolVersion() != 1)
        throw new SecurityException("Display protocol version");
      service.linkToDeath(recipient, 0);
      if (!lease.publish(uid, pid, token, service)) {
        service.unlinkToDeath(recipient, 0);
        throw new SecurityException("Display lease changed");
      }
      synchronized (this) {
        if (!lease.open() || closing.get() || !service.isBinderAlive()) {
          lease.died(service);
          service.unlinkToDeath(recipient, 0);
          close();
          throw new RemoteException("Display publisher died");
        }
        binder = service;
        death = recipient;
        remote = candidate;
      }
      worker.execute(
          () -> {
            if (!lease.open()) return;
            for (boolean cursor : new boolean[] {false, true}) sendCurrent(candidate, cursor);
          });
    } finally {
      Binder.restoreCallingIdentity(identity);
    }
  }

  void revoke(int uid, int pid, String token) {
    if (!lease.revoke(uid, pid, token)) throw new SecurityException("Display revoke refused");
    close();
  }

  long attach(Surface source, boolean cursor) {
    if (!lease.open() || !source.isValid()) return 0;
    // Keep our own native reference until the worker has parceled the Surface.
    Parcel parcel = Parcel.obtain();
    Surface copy;
    try {
      source.writeToParcel(parcel, 0);
      parcel.setDataPosition(0);
      copy = Surface.CREATOR.createFromParcel(parcel);
    } finally {
      parcel.recycle();
    }
    long generation = sequence.incrementAndGet();
    try {
      worker.execute(
          () -> {
            if (!lease.open()) {
              copy.release();
              return;
            }
            if (timeline.attach(generation, copy, cursor) && remote != null)
              sendCurrent(remote, cursor);
          });
    } catch (RejectedExecutionException closed) {
      copy.release();
      return 0;
    }
    return generation;
  }

  void detach(long generation, boolean cursor) {
    try {
      worker.execute(
          () -> {
            if (timeline.detach(generation, cursor) && remote != null) {
              try {
                remote.detach(capability.session, generation, cursor);
              } catch (RemoteException | RuntimeException error) {
                fail();
              }
            }
          });
    } catch (RejectedExecutionException ignored) {
    }
  }

  private void sendCurrent(IOwnedDisplay endpoint, boolean cursor) {
    Surface frame = timeline.frame(cursor);
    if (frame == null || !lease.open()) return;
    try {
      endpoint.attach(capability.session, timeline.generation(cursor), frame, cursor);
    } catch (RemoteException | RuntimeException error) {
      fail();
    }
  }

  private void fail() {
    android.util.Log.w("ProtectedLinuxAndroid", "OWNED_DISPLAY_CONNECTION_LOST");
    close();
  }

  public void close() {
    if (!closing.compareAndSet(false, true)) return;
    lease.close();
    CURRENT.compareAndSet(this, null);
    worker.execute(
        () -> {
          IOwnedDisplay endpoint = remote;
          for (boolean cursor : new boolean[] {false, true}) {
            long generation = timeline.generation(cursor);
            if (endpoint != null && generation > 0) {
              try {
                endpoint.detach(capability.session, generation, cursor);
              } catch (RemoteException | RuntimeException ignored) {
              }
            }
          }
          timeline.close();
          synchronized (this) {
            remote = null;
            if (binder != null && death != null) binder.unlinkToDeath(death, 0);
            binder = null;
            death = null;
          }
        });
    worker.shutdown();
  }
}
