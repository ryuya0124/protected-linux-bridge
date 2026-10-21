package net.ryuya_dev.protectedlinux;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.color.DynamicColors;
import java.io.BufferedReader;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Separate experimental Android display. Debian's terminal sessions remain separate. */
public final class AndroidGuestActivity extends AppCompatActivity {
  static final String EXTRA_GPU_SURFACE = "android_guest_gpu_surface";
  static final String EXTRA_INTERNET = "android_guest_internet";
  static final String EXTRA_MEMORY_MIB = "android_memory_mib";
  static final String EXTRA_POLICY_ACKNOWLEDGED = "android_small_shmem_acknowledged";
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private AndroidGuestCommands commands = new AndroidGuestCommands(false);
  private final AndroidGuestPointers pointers = new AndroidGuestPointers();
  private volatile boolean closed, resumed, inputReady;
  private volatile LocalSocket connection;
  private volatile Process manager;
  private TextView status, networkStatus;
  private MaterialButton networkRetry;
  private final java.util.List<MaterialButton> inputControls = new java.util.ArrayList<>();
  private volatile boolean networkRestartRequested;
  private ImageView display;
  private AndroidGuestGpuSurface gpuSurface;
  private Bitmap currentFrame;
  private int memoryMib;
  private volatile int runtimeFailure;
  private final AndroidGuestTouch touch = new AndroidGuestTouch();

  @Override
  public void onCreate(Bundle state) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(state);
    EdgeToEdge.enable(this);
    memoryMib = getIntent().getIntExtra(EXTRA_MEMORY_MIB, 1024);
    LinearLayout page = new LinearLayout(this);
    page.setOrientation(LinearLayout.VERTICAL);
    int padding = Math.round(12 * getResources().getDisplayMetrics().density);
    page.setPadding(padding, 0, padding, 0);
    status = new TextView(this);
    status.setText(R.string.android_guest_starting);
    status.setPadding(0, padding, 0, padding);
    page.addView(status);
    networkStatus = new TextView(this);
    networkStatus.setText(R.string.android_guest_network_preparing);
    networkStatus.setVisibility(
        memoryMib == 1024 && getIntent().getBooleanExtra(EXTRA_INTERNET, false)
            ? View.VISIBLE
            : View.GONE);
    page.addView(networkStatus);
    networkRetry =
        new MaterialButton(
            this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle);
    networkRetry.setText(R.string.android_guest_network_retry);
    networkRetry.setVisibility(networkStatus.getVisibility());
    networkRetry.setEnabled(false);
    networkRetry.setOnClickListener(
        view -> {
          networkRestartRequested = true;
          networkRetry.setEnabled(false);
          networkStatus.setText(R.string.android_guest_network_preparing);
        });
    page.addView(networkRetry);
    display = new ImageView(this);
    display.setScaleType(ImageView.ScaleType.FIT_CENTER);
    display.setBackgroundColor(android.graphics.Color.BLACK);
    display.setContentDescription(getString(R.string.android_guest_title));
    android.widget.FrameLayout frame = new android.widget.FrameLayout(this);
    frame.setBackgroundColor(android.graphics.Color.BLACK);
    frame.addView(display, new android.widget.FrameLayout.LayoutParams(-1, -1));
    page.addView(frame, new LinearLayout.LayoutParams(-1, 0, 1));
    if (memoryMib == 1024 && getIntent().getBooleanExtra(EXTRA_GPU_SURFACE, true)) {
      try {
        gpuSurface = new AndroidGuestGpuSurface(this, frame);
        commands = new AndroidGuestCommands(true);
      } catch (IOException busy) {
        status.setText(busy.getMessage());
        setContentView(page);
        return;
      }
    }
    View touchTarget = gpuSurface == null ? display : gpuSurface.view;
    touchTarget.setOnTouchListener(
        (view, event) -> {
          if (!inputReady || closed) {
            pointers.cancel();
            touch.cancel();
            return true;
          }
          if (gpuSurface != null) {
            int count = event.getPointerCount();
            int[] ids = new int[count];
            float[] x = new float[count], y = new float[count];
            for (int i = 0; i < count; i++) {
              ids[i] = event.getPointerId(i);
              x[i] = event.getX(i);
              y[i] = event.getY(i);
            }
            byte[] sample =
                pointers.sample(
                    event.getActionMasked(),
                    event.getActionIndex(),
                    ids,
                    x,
                    y,
                    view.getWidth(),
                    view.getHeight());
            if (sample != null) enqueue(sample);
            if (event.getActionMasked() == MotionEvent.ACTION_UP) view.performClick();
            return true;
          }
          if (event.getPointerCount() != 1
              || event.getActionMasked() == MotionEvent.ACTION_CANCEL) {
            touch.cancel();
          } else if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            touch.begin(
                event.getX(),
                event.getY(),
                view.getWidth(),
                view.getHeight(),
                event.getEventTime());
          } else if (event.getActionMasked() == MotionEvent.ACTION_UP) {
            int[] gesture =
                touch.end(
                    event.getX(),
                    event.getY(),
                    view.getWidth(),
                    view.getHeight(),
                    event.getEventTime(),
                    ViewConfiguration.get(this).getScaledTouchSlop());
            if (gesture != null) enqueue(packet(gesture.length == 2 ? 'T' : 'S', gesture));
            view.performClick();
          }
          return true;
        });
    LinearLayout controls = new LinearLayout(this);
    page.addView(controls);
    control(controls, R.string.android_guest_unlock, () -> enqueue(packet('U')));
    control(controls, R.string.android_guest_back, () -> enqueue(packet('K', 4)));
    control(controls, R.string.android_guest_home, () -> enqueue(packet('K', 3)));
    control(controls, R.string.android_guest_close, this::finish);
    setContentView(page);
    ViewCompat.setOnApplyWindowInsetsListener(
        page,
        (view, insets) -> {
          var bars =
              insets.getInsets(
                  WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout());
          view.setPadding(bars.left + padding, bars.top, bars.right + padding, bars.bottom);
          return insets;
        });
    worker.submit(this::session);
  }

  private void control(LinearLayout row, int label, Runnable action) {
    MaterialButton button = new MaterialButton(this);
    button.setText(label);
    if (label != R.string.android_guest_close) {
      button.setEnabled(false);
      inputControls.add(button);
    }
    row.addView(button, new LinearLayout.LayoutParams(0, -2, 1));
    button.setOnClickListener(view -> action.run());
  }

  private void enqueue(byte[] command) {
    if (!inputReady || closed) return;
    if (!commands.offer(command)) {
      pointers.cancel();
      status.setText(R.string.android_guest_input_busy);
    }
  }

  private static byte[] packet(char command, int... values) {
    ByteBuffer bytes = ByteBuffer.allocate(1 + values.length * 4).order(ByteOrder.LITTLE_ENDIAN);
    bytes.put((byte) command);
    for (int value : values) bytes.putInt(value);
    return bytes.array();
  }

  private static byte[] read(InputStream input, int size) throws IOException {
    byte[] bytes = input.readNBytes(size);
    if (bytes.length != size) throw new EOFException();
    return bytes;
  }

  private static int number(InputStream input) throws IOException {
    return ByteBuffer.wrap(read(input, 4)).order(ByteOrder.LITTLE_ENDIAN).getInt();
  }

  private void session() {
    try {
      Process process =
          AndroidGuestRuntime.start(
              this,
              memoryMib,
              getIntent().getBooleanExtra(EXTRA_POLICY_ACKNOWLEDGED, false),
              gpuSurface != null);
      manager = process;
      if (closed) {
        process.getOutputStream().close();
        return;
      }
      OwnedDisplayConnection owned = OwnedDisplayConnection.current();
      if (gpuSurface == null || owned == null) throw new IOException("Owned display missing");
      owned.writeCapability(process.getOutputStream());
      process.getOutputStream().close();
      Thread logs =
          new Thread(
              () -> {
                try (var reader =
                    new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                  String line;
                  while ((line = reader.readLine()) != null) {
                    android.util.Log.i("ProtectedLinuxAndroid", line);
                    if (line.contains("ANDROID_BACKING_UNSUITABLE")
                        || line.contains("PLB_PREALLOCATED_THP_REFUSED vmm_started=0"))
                      runtimeFailure = 89;
                    if (line.contains("PLB_GUNYAH_PAGE_FAULT")) runtimeFailure = 33;
                    if (line.contains("PLB_ANDROID_ADOPTED_HOST_FAILURE")) runtimeFailure = 42;
                    if (line.contains("PLB_ANDROID_ADOPTED_RESOURCE_LIMIT")) runtimeFailure = 23;
                    if (line.contains("PLB_POLICY_RESTORED=0")
                        || line.contains("PLB_POLICY_GUARDIAN_RESTORED=0")) runtimeFailure = 38;
                    if (line.contains("ANDROID_SMALL_SHMEM_RESTORED=0")
                        || line.contains("ANDROID_SMALL_SHMEM_GUARDIAN_RESTORED=0"))
                      runtimeFailure = 38;
                    if (line.startsWith("ANDROID_MEMORY_PREPARING")
                        || line.contains("PLB_ALLOCATION_STAGE")
                        || line.contains("PLB_PREALLOCATED_BATCH"))
                      runOnUiThread(
                          () -> {
                            if (!closed) status.setText(R.string.android_guest_preparing);
                          });
                  }
                } catch (IOException ignored) {
                }
              },
              "android-guest-log");
      logs.start();
      LocalSocket socket = null;
      for (int attempt = 0; attempt < 900 && !closed && process.isAlive(); attempt++) {
        LocalSocket candidate = new LocalSocket();
        try {
          candidate.connect(
              new LocalSocketAddress(
                  "net.ryuya_dev.protectedlinux.android-display",
                  LocalSocketAddress.Namespace.ABSTRACT));
          if (candidate.getPeerCredentials().getUid() != 0) throw new IOException("peer");
          socket = candidate;
          connection = socket;
          break;
        } catch (IOException unavailable) {
          candidate.close();
          Thread.sleep(100);
        }
      }
      if (socket == null)
        throw new IOException(
            getString(
                !process.isAlive()
                    ? AndroidGuestRuntime.errorMessage(process.exitValue())
                    : R.string.android_guest_busy));
      socket.setSoTimeout(120000);
      InputStream input = socket.getInputStream();
      OutputStream output = socket.getOutputStream();
      if (number(input) != 0x41424c50
          || number(input) != 1
          || number(input) != 480
          || number(input) != 800
          || number(input) != 0)
        throw new IOException(getString(R.string.android_guest_start_failed));
      if (gpuSurface == null) throw new IOException("Android 17 requires owned GPU display");
      inputReady = true;
      runOnUiThread(
          () -> {
            if (!closed) inputControls.forEach(button -> button.setEnabled(inputReady));
          });
      boolean networkPending = false;
      long networkCheckDue = 0;
      if (memoryMib == 1024 && getIntent().getBooleanExtra(EXTRA_INTERNET, false)) {
        output.write('N');
        int result = input.read();
        if (result < 0) throw new EOFException();
        networkPending = result == 2;
        networkResult(result);
      }
      if (gpuSurface != null)
        runOnUiThread(
            () ->
                status.setText(
                    getString(R.string.android_guest_gpu_connected, memoryMib + " MiB")));
      boolean rawFrames = true;
      byte[] rawFrame = new byte[AndroidGuestFrame.SIZE];
      byte[] pendingCommand = null;
      long measuredAt = android.os.SystemClock.elapsedRealtime();
      long captureDue = 0;
      int measuredFrames = 0;
      while (!closed) {
        byte[] command = pendingCommand;
        pendingCommand = null;
        if (command == null) command = commands.poll();
        // Bound each input batch so a busy producer cannot starve frame delivery.
        for (int count = 0; command != null && count < 12; count++) {
          output.write(command);
          int reply = input.read();
          if (reply < 0) throw new EOFException();
          if (reply != 0) {
            android.util.Log.w("ProtectedLinuxAndroid", "INPUT_REJECTED");
            runOnUiThread(() -> status.setText(R.string.android_guest_input_failed));
          }
          command = count < 11 ? commands.poll() : null;
        }
        long networkNow = android.os.SystemClock.elapsedRealtime();
        if (networkRestartRequested) {
          networkRestartRequested = false;
          output.write('N');
          int result = input.read();
          if (result < 0) throw new EOFException();
          networkPending = result == 2;
          networkResult(result);
          networkCheckDue = 0;
        }
        if (getIntent().getBooleanExtra(EXTRA_INTERNET, false) && networkNow >= networkCheckDue) {
          output.write('n');
          int result = input.read();
          if (result < 0) throw new EOFException();
          if (result != 2) {
            networkPending = false;
            networkResult(result);
          }
          networkCheckDue = networkNow + (networkPending ? 1000 : 10000);
        }
        if (!resumed || gpuSurface != null) {
          output.write('P');
          if (input.read() != 'P') throw new IOException("ping");
          pendingCommand = commands.poll(1000, java.util.concurrent.TimeUnit.MILLISECONDS);
          continue;
        }
        long captureStarted = android.os.SystemClock.elapsedRealtime();
        if (captureStarted < captureDue) {
          pendingCommand =
              commands.poll(
                  captureDue - captureStarted, java.util.concurrent.TimeUnit.MILLISECONDS);
          continue;
        }
        captureDue = captureStarted + 100;
        output.write(rawFrames ? 'R' : 'F');
        int size = number(input);
        if (size < 0 || size > 2 * 1024 * 1024) throw new IOException("frame size");
        if (size > 0) {
          final Bitmap pngBitmap;
          final ByteBuffer pixels;
          if (rawFrames) {
            if (size != rawFrame.length) throw new IOException("raw frame length");
            int at = 0;
            while (at < size) {
              int count = input.read(rawFrame, at, size - at);
              if (count < 0) throw new EOFException();
              at += count;
            }
            pixels = AndroidGuestFrame.pixels(rawFrame, size);
            pngBitmap = null;
          } else {
            byte[] png = read(input, size);
            BitmapFactory.Options dimensions = new BitmapFactory.Options();
            dimensions.inJustDecodeBounds = true;
            BitmapFactory.decodeByteArray(png, 0, png.length, dimensions);
            if (dimensions.outWidth != 480 || dimensions.outHeight != 800)
              throw new IOException("frame dimensions");
            pngBitmap = BitmapFactory.decodeByteArray(png, 0, png.length);
            if (pngBitmap == null) throw new IOException("PNG");
            pixels = null;
          }
          var applied = new java.util.concurrent.CountDownLatch(1);
          runOnUiThread(
              () -> {
                try {
                  if (closed) {
                    if (pngBitmap != null) pngBitmap.recycle();
                    return;
                  }
                  if (pixels != null) {
                    // Reuse one mutable bitmap on the UI thread. The worker waits
                    // for the copy before reusing its single bounded byte buffer.
                    if (currentFrame == null || !currentFrame.isMutable()) {
                      currentFrame = Bitmap.createBitmap(480, 800, Bitmap.Config.ARGB_8888);
                      display.setImageBitmap(currentFrame);
                    }
                    currentFrame.copyPixelsFromBuffer(pixels);
                    display.invalidate();
                  } else {
                    currentFrame = pngBitmap;
                    display.setImageBitmap(pngBitmap);
                  }
                  status.setText(getString(R.string.android_guest_connected, memoryMib + " MiB"));
                } finally {
                  applied.countDown();
                }
              });
          if (!applied.await(5, java.util.concurrent.TimeUnit.SECONDS)) throw new IOException("UI");
          measuredFrames++;
        } else {
          captureDue = android.os.SystemClock.elapsedRealtime() + 500;
          if (rawFrames) {
            rawFrames = false;
            android.util.Log.i("ProtectedLinuxAndroid", "DISPLAY_RAW_UNAVAILABLE: PNG fallback");
          }
        }
        long now = android.os.SystemClock.elapsedRealtime();
        if (now - measuredAt >= 5000) {
          android.util.Log.i(
              "ProtectedLinuxAndroid",
              "DISPLAY_STATS mode="
                  + (rawFrames ? "raw" : "png")
                  + " frames="
                  + measuredFrames
                  + " elapsed_ms="
                  + (now - measuredAt));
          measuredAt = now;
          measuredFrames = 0;
        }
      }
    } catch (Exception error) {
      inputReady = false;
      android.util.Log.w("ProtectedLinuxAndroid", "SESSION_STOPPED", error);
      int failure = runtimeFailure;
      Process process = manager;
      if (failure == 0 && process != null && !closed) {
        try {
          if (process.waitFor(3, java.util.concurrent.TimeUnit.SECONDS))
            failure = process.exitValue();
        } catch (InterruptedException interrupted) {
          Thread.currentThread().interrupt();
        }
      }
      if (runtimeFailure != 0) failure = runtimeFailure;
      String detail =
          failure != 0
              ? getString(AndroidGuestRuntime.errorMessage(failure))
              : error instanceof EOFException
                  ? getString(R.string.android_guest_start_failed)
                  : error.getMessage();
      runOnUiThread(
          () -> {
            if (!closed) {
              inputControls.forEach(button -> button.setEnabled(false));
              status.setText(getString(R.string.android_guest_error, detail));
              if (networkStatus.getVisibility() == View.VISIBLE) {
                networkStatus.setText(R.string.android_guest_network_stopped);
                networkRetry.setEnabled(false);
              }
            }
          });
    } finally {
      inputReady = false;
      disconnect();
    }
  }

  private void networkResult(int result) {
    runOnUiThread(
        () -> {
          if (closed) return;
          networkStatus.setText(
              result == 0
                  ? R.string.android_guest_network_ready
                  : result == 2
                      ? R.string.android_guest_network_preparing
                      : R.string.android_guest_network_failed);
          networkRetry.setEnabled(result != 2);
        });
  }

  private void disconnect() {
    LocalSocket socket = connection;
    connection = null;
    if (socket != null)
      try {
        socket.close();
      } catch (IOException ignored) {
      }
  }

  @Override
  protected void onResume() {
    super.onResume();
    resumed = true;
  }

  @Override
  protected void onPause() {
    resumed = false;
    byte[] cancel = pointers.cancel();
    if (cancel != null) enqueue(cancel);
    super.onPause();
  }

  @Override
  protected void onDestroy() {
    closed = true;
    disconnect();
    worker.shutdownNow();
    display.setImageDrawable(null);
    currentFrame = null;
    if (gpuSurface != null) gpuSurface.close();
    super.onDestroy();
  }
}
