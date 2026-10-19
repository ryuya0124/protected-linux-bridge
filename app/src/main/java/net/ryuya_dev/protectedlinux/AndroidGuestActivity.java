package net.ryuya_dev.protectedlinux;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.view.MotionEvent;
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
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Separate experimental Android display. Debian's terminal sessions remain separate. */
public final class AndroidGuestActivity extends AppCompatActivity {
  static final String EXTRA_MEMORY_MIB = "android_memory_mib";
  static final String EXTRA_POLICY_ACKNOWLEDGED = "android_small_shmem_acknowledged";
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private final ArrayBlockingQueue<byte[]> commands = new ArrayBlockingQueue<>(12);
  private volatile boolean closed, resumed;
  private volatile LocalSocket connection;
  private volatile Process manager;
  private TextView status;
  private ImageView display;
  private Bitmap currentFrame;
  private int memoryMib;
  private volatile int runtimeFailure;

  @Override
  public void onCreate(Bundle state) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(state);
    EdgeToEdge.enable(this);
    memoryMib = getIntent().getIntExtra(EXTRA_MEMORY_MIB, 512);
    LinearLayout page = new LinearLayout(this);
    page.setOrientation(LinearLayout.VERTICAL);
    int padding = Math.round(12 * getResources().getDisplayMetrics().density);
    page.setPadding(padding, 0, padding, 0);
    status = new TextView(this);
    status.setText(R.string.android_guest_starting);
    status.setPadding(0, padding, 0, padding);
    page.addView(status);
    display = new ImageView(this);
    display.setScaleType(ImageView.ScaleType.FIT_CENTER);
    display.setBackgroundColor(android.graphics.Color.BLACK);
    display.setContentDescription(getString(R.string.android_guest_title));
    page.addView(display, new LinearLayout.LayoutParams(-1, 0, 1));
    display.setOnTouchListener(
        (view, event) -> {
          if (event.getAction() == MotionEvent.ACTION_UP) {
            float scale = Math.min(display.getWidth() / 480f, display.getHeight() / 800f);
            if (scale <= 0) return true;
            int x = (int) ((event.getX() - (display.getWidth() - 480 * scale) / 2) / scale);
            int y = (int) ((event.getY() - (display.getHeight() - 800 * scale) / 2) / scale);
            if (x >= 0 && x < 480 && y >= 0 && y < 800) commands.offer(packet('T', x, y));
            view.performClick();
          }
          return true;
        });
    LinearLayout controls = new LinearLayout(this);
    page.addView(controls);
    control(controls, R.string.android_guest_unlock, () -> commands.offer(packet('U')));
    control(controls, R.string.android_guest_back, () -> commands.offer(packet('K', 4)));
    control(controls, R.string.android_guest_home, () -> commands.offer(packet('K', 3)));
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
    row.addView(button, new LinearLayout.LayoutParams(0, -2, 1));
    button.setOnClickListener(view -> action.run());
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
              this, memoryMib, getIntent().getBooleanExtra(EXTRA_POLICY_ACKNOWLEDGED, false));
      manager = process;
      Thread logs =
          new Thread(
              () -> {
                try (var reader =
                    new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                  String line;
                  while ((line = reader.readLine()) != null) {
                    android.util.Log.i("ProtectedLinuxAndroid", line);
                    if (line.contains("ANDROID_BACKING_UNSUITABLE")) runtimeFailure = 89;
                    if (line.contains("ANDROID_SMALL_SHMEM_RESTORED=0")
                        || line.contains("ANDROID_SMALL_SHMEM_GUARDIAN_RESTORED=0"))
                      runtimeFailure = 38;
                    if (line.startsWith("ANDROID_MEMORY_PREPARING"))
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
      for (int attempt = 0; attempt < 600 && !closed && process.isAlive(); attempt++) {
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
      while (!closed) {
        byte[] command;
        while ((command = commands.poll()) != null) {
          output.write(command);
          int reply = input.read();
          if (reply < 0) throw new EOFException();
          if (reply != 0) android.util.Log.w("ProtectedLinuxAndroid", "INPUT_DELAYED");
        }
        if (!resumed) {
          output.write('P');
          if (input.read() != 'P') throw new IOException("ping");
          Thread.sleep(1000);
          continue;
        }
        output.write('F');
        int size = number(input);
        if (size < 0 || size > 2 * 1024 * 1024) throw new IOException("frame size");
        if (size > 0) {
          byte[] png = read(input, size);
          BitmapFactory.Options dimensions = new BitmapFactory.Options();
          dimensions.inJustDecodeBounds = true;
          BitmapFactory.decodeByteArray(png, 0, png.length, dimensions);
          if (dimensions.outWidth != 480 || dimensions.outHeight != 800)
            throw new IOException("frame dimensions");
          Bitmap bitmap = BitmapFactory.decodeByteArray(png, 0, png.length);
          if (bitmap == null) throw new IOException("PNG");
          var applied = new java.util.concurrent.CountDownLatch(1);
          runOnUiThread(
              () -> {
                try {
                  if (closed) {
                    bitmap.recycle();
                    return;
                  }
                  currentFrame = bitmap;
                  display.setImageBitmap(bitmap);
                  status.setText(
                      getString(
                          R.string.android_guest_connected,
                          memoryMib == 1536 ? "1.5 GiB" : "512 MiB"));
                } finally {
                  applied.countDown();
                }
              });
          if (!applied.await(5, java.util.concurrent.TimeUnit.SECONDS)) throw new IOException("UI");
        }
        Thread.sleep(1500);
      }
    } catch (Exception error) {
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
            if (!closed) status.setText(getString(R.string.android_guest_error, detail));
          });
    } finally {
      disconnect();
    }
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
    super.onPause();
  }

  @Override
  protected void onDestroy() {
    closed = true;
    disconnect();
    worker.shutdownNow();
    display.setImageDrawable(null);
    currentFrame = null;
    super.onDestroy();
  }
}
