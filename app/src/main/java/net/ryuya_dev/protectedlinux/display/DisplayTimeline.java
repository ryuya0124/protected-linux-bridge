package net.ryuya_dev.protectedlinux.display;

import java.util.function.Consumer;

/** Serial-worker surface ownership. Detach seals a generation even if attach arrives late. */
public final class DisplayTimeline<T> implements AutoCloseable {
  private final Consumer<T> release;
  private final Object[] frames = new Object[2];
  private final long[] generations = new long[2];
  private boolean closed;

  public DisplayTimeline(Consumer<T> release) {
    this.release = release;
  }

  public boolean attach(long generation, T frame, boolean cursor) {
    int i = cursor ? 1 : 0;
    if (closed || generation <= 0 || generation <= generations[i]) {
      release.accept(frame);
      return false;
    }
    discard(i);
    generations[i] = generation;
    frames[i] = frame;
    return true;
  }

  public boolean detach(long generation, boolean cursor) {
    int i = cursor ? 1 : 0;
    if (closed || generation <= 0 || generation < generations[i]) return false;
    generations[i] = generation;
    discard(i);
    return true;
  }

  @SuppressWarnings("unchecked")
  public T frame(boolean cursor) {
    return (T) frames[cursor ? 1 : 0];
  }

  public long generation(boolean cursor) {
    return generations[cursor ? 1 : 0];
  }

  private void discard(int i) {
    @SuppressWarnings("unchecked")
    T frame = (T) frames[i];
    frames[i] = null;
    if (frame != null) release.accept(frame);
  }

  public void close() {
    closed = true;
    discard(0);
    discard(1);
  }
}
