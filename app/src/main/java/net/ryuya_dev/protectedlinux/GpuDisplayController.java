package net.ryuya_dev.protectedlinux;

import android.app.*;
import android.content.Context;
import android.net.*;
import android.opengl.*;
import android.os.*;
import android.system.*;
import android.text.InputType;
import android.view.*;
import android.widget.*;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.color.MaterialColors;
import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;
import java.io.*;
import java.nio.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** Bounded direct framebuffer -> GLES texture display, entirely outside the root manager. */
final class GpuDisplayController {
  private static final Map<Activity, GpuDisplayController> active = new WeakHashMap<>();
  private final ExecutorService reader = Executors.newSingleThreadExecutor();
  private final ExecutorService writer =
      new ThreadPoolExecutor(1, 1, 0, TimeUnit.MILLISECONDS, new ArrayBlockingQueue<>(32));
  private final AtomicReference<Upload> pending = new AtomicReference<>();
  private final java.util.concurrent.atomic.AtomicBoolean failedState =
      new java.util.concurrent.atomic.AtomicBoolean();
  private volatile boolean paused;
  private boolean standalone;
  private volatile LocalSocket socket;
  private volatile RawRfb protocol;
  private volatile boolean closed;
  private Dialog dialog;
  private GLSurfaceView view;
  private TextView status;
  private Activity activity;
  private volatile int fbWidth, fbHeight;
  private boolean ja;
  private Context themed;
  private final Map<Long, Integer> heldKeys = new HashMap<>();
  private int deadAccent, lastX, lastY;
  private int targetFps = 30;
  private volatile int rotationWidth, rotationHeight;

  private record Upload(
      ByteBuffer pixels, List<RawRfb.Rect> damage, int width, int height, CountDownLatch done) {}

  static void open(Activity a, String apk) {
    if (active.containsKey(a)) return;
    GpuDisplayController c = new GpuDisplayController();
    active.put(a, c);
    try {
      c.show(a, apk);
    } catch (RuntimeException e) {
      active.remove(a);
      c.shutdown();
      android.util.Log.w("ProtectedLinux", "RAW_GPU_SETUP_FAILED", e);
      DisplayController.openLegacy(a, apk);
    }
  }

  static void openStandalone(Activity a, String apk) {
    if (active.containsKey(a)) return;
    GpuDisplayController c = new GpuDisplayController();
    c.standalone = true;
    active.put(a, c);
    try {
      c.show(a, apk);
    } catch (RuntimeException error) {
      active.remove(a);
      c.shutdown();
      DisplayController.openStandaloneLegacy(a, apk);
      android.util.Log.w("ProtectedLinux", "FULLSCREEN_GPU_FAILED", error);
    }
  }

  static void close(Activity a) {
    GpuDisplayController c = active.remove(a);
    if (c != null) {
      c.shutdown();
      if (c.dialog != null) c.dialog.dismiss();
    }
  }

  static void settingsChanged(Activity activity) {
    GpuDisplayController c = active.get(activity);
    if (c != null) c.requestRotation();
  }

  private String tr(String en, String jp) {
    return ja ? jp : en;
  }

  private void show(Activity a, String apk) {
    activity = a;
    targetFps = RuntimePreferences.displayFps();
    ja = a.getResources().getConfiguration().getLocales().get(0).getLanguage().equals("ja");
    try {
      if (standalone) themed = a;
      else {
        // Package visibility can hide the module from Terminal. Load only this APK's
        // resources into an isolated Resources object; never modify Terminal resources.
        android.content.res.Resources res =
            new android.content.res.Resources(
                android.content.res.Resources.getSystem().getAssets(),
                a.getResources().getDisplayMetrics(),
                a.getResources().getConfiguration());
        android.content.res.loader.ResourcesLoader loader =
            new android.content.res.loader.ResourcesLoader();
        try (ParcelFileDescriptor fd =
            ParcelFileDescriptor.open(new File(apk), ParcelFileDescriptor.MODE_READ_ONLY)) {
          loader.addProvider(android.content.res.loader.ResourcesProvider.loadFromApk(fd));
        }
        res.addLoaders(loader);
        android.content.res.Resources.Theme moduleTheme = res.newTheme();
        moduleTheme.applyStyle(R.style.Theme_ProtectedLinux, true);
        themed =
            new ContextThemeWrapper(a, R.style.Theme_ProtectedLinux) {
              @Override
              public android.content.res.Resources getResources() {
                return res;
              }

              @Override
              public android.content.res.Resources.Theme getTheme() {
                return moduleTheme;
              }

              @Override
              public android.content.res.AssetManager getAssets() {
                return res.getAssets();
              }

              @Override
              public ClassLoader getClassLoader() {
                return GpuDisplayController.class.getClassLoader();
              }
            };
      }
    } catch (Exception error) {
      throw new IllegalStateException("Module theme unavailable", error);
    }
    if (!standalone) {
      dialog = new Dialog(a);
      dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
    }
    LinearLayout body = new LinearLayout(themed);
    body.setOrientation(LinearLayout.VERTICAL);
    body.setPadding(dp(12), dp(8), dp(12), dp(8));
    body.setBackgroundColor(
        MaterialColors.getColor(
            themed, com.google.android.material.R.attr.colorSurface, 0xff1c1b1f));
    status = new TextView(themed);
    status.setText(tr("Connecting GPU display…", "GPU画面を接続しています…"));
    body.addView(status);
    view =
        new GLSurfaceView(a) {
          @Override
          protected void onWindowVisibilityChanged(int visibility) {
            super.onWindowVisibilityChanged(visibility);
            paused = visibility != View.VISIBLE;
            if (!paused && view != null) view.requestRender();
          }

          @Override
          public void onWindowFocusChanged(boolean focused) {
            super.onWindowFocusChanged(focused);
            if (!focused) releaseInputs();
          }
        };
    view.setEGLContextClientVersion(3);
    view.setPreserveEGLContextOnPause(true);
    view.setRenderer(new Renderer());
    view.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
    body.addView(view, new LinearLayout.LayoutParams(-1, 0, 1));
    view.setFocusableInTouchMode(true);
    view.setFocusable(true);
    view.setOnTouchListener(
        (v, event) -> {
          if (protocol == null) return true;
          if (event.getActionMasked() == MotionEvent.ACTION_DOWN) view.requestFocus();
          int action = event.getActionMasked(),
              buttons =
                  action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL
                      ? 0
                      : (event.isFromSource(InputDevice.SOURCE_MOUSE)
                          ? PhysicalInput.buttons(event.getButtonState())
                          : 1);
          pointer(
              event,
              buttons,
              action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL);
          if (action == MotionEvent.ACTION_UP) v.performClick();
          return true;
        });
    view.setOnHoverListener(
        (v, event) -> {
          if (!event.isFromSource(InputDevice.SOURCE_MOUSE)) return false;
          pointer(event, PhysicalInput.buttons(event.getButtonState()), false);
          return true;
        });
    view.setOnGenericMotionListener(
        (v, event) -> {
          if (!event.isFromSource(InputDevice.SOURCE_MOUSE)) return false;
          int mask = PhysicalInput.buttons(event.getButtonState());
          pointer(event, mask, false);
          if (event.getActionMasked() == MotionEvent.ACTION_SCROLL) {
            wheel(event.getAxisValue(MotionEvent.AXIS_VSCROLL), 8, 16, mask);
            wheel(event.getAxisValue(MotionEvent.AXIS_HSCROLL), 64, 32, mask);
          }
          return true;
        });
    view.setOnFocusChangeListener(
        (v, focused) -> {
          if (!focused) releaseInputs();
        });
    view.setOnKeyListener((v, code, event) -> physicalKey(code, event));
    view.requestFocus();
    TextInputLayout input = new TextInputLayout(themed);
    input.setHint(tr("Type text, then send", "入力して送信"));
    EditText draft = new TextInputEditText(input.getContext());
    draft.setInputType(
        InputType.TYPE_CLASS_TEXT
            | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
    draft.setMaxLines(3);
    input.addView(draft, new LinearLayout.LayoutParams(-1, -2));
    body.addView(input);
    LinearLayout controls = new LinearLayout(themed);
    body.addView(controls);
    button(controls, tr("Send", "送信"), () -> text(draft, false));
    button(controls, "Enter", () -> text(draft, true));
    button(controls, "Tab", () -> key(0xff09));
    button(controls, "Esc", () -> key(0xff1b));
    LinearLayout clips = new LinearLayout(themed);
    body.addView(clips);
    button(clips, "Android → Linux", () -> ClipboardBridge.transfer(a, true, status::setText));
    button(clips, "Linux → Android", () -> ClipboardBridge.transfer(a, false, status::setText));
    MaterialButton fallback = new MaterialButton(themed);
    fallback.setAllCaps(false);
    fallback.setText(tr("Compatibility display", "互換表示で開き直す"));
    body.addView(fallback);
    fallback.setOnClickListener(
        v -> {
          close(a);
          if (standalone) DisplayController.openStandaloneLegacy(a, apk);
          else DisplayController.openLegacy(a, apk);
        });
    if (standalone) {
      body.removeView(view);
      FrameLayout fullscreen = new FrameLayout(themed);
      fullscreen.addView(view, new FrameLayout.LayoutParams(-1, -1));
      // Controls are an overlay; collapsing them leaves the full surface for the desktop.
      LinearLayout panel = new LinearLayout(themed);
      panel.setOrientation(LinearLayout.VERTICAL);
      panel.setPadding(dp(12), dp(6), dp(12), dp(6));
      panel.setBackgroundColor(
          MaterialColors.getColor(
              themed, com.google.android.material.R.attr.colorSurface, 0xff1c1b1f));
      body.removeView(input);
      body.removeView(controls);
      body.removeView(clips);
      body.removeView(fallback);
      panel.addView(input);
      panel.addView(controls);
      panel.addView(clips);
      panel.addView(fallback);
      panel.setVisibility(View.GONE);
      FrameLayout.LayoutParams dock = new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM);
      dock.bottomMargin = dp(64);
      fullscreen.addView(panel, dock);
      MaterialButton keyboard = new MaterialButton(themed);
      keyboard.setText(tr("Input / controls", "入力・操作"));
      keyboard.setAllCaps(false);
      FrameLayout.LayoutParams toggle =
          new FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM | Gravity.END);
      toggle.setMargins(dp(12), 0, dp(12), dp(8));
      fullscreen.addView(keyboard, toggle);
      keyboard.setOnClickListener(
          v -> {
            boolean show = panel.getVisibility() != View.VISIBLE;
            panel.setVisibility(show ? View.VISIBLE : View.GONE);
            if (show) draft.requestFocus();
            else {
              activity
                  .getSystemService(android.view.inputmethod.InputMethodManager.class)
                  .hideSoftInputFromWindow(draft.getWindowToken(), 0);
              view.requestFocus();
            }
          });
      MaterialButton back = new MaterialButton(themed);
      back.setText(tr("Close GUI", "GUIを閉じる"));
      back.setAllCaps(false);
      FrameLayout.LayoutParams backPos =
          new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.END);
      backPos.setMargins(dp(8), dp(4), dp(8), 0);
      fullscreen.addView(back, backPos);
      back.setOnClickListener(v -> a.finish());
      FrameLayout.LayoutParams statusPos =
          new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.START);
      statusPos.setMargins(dp(12), dp(8), dp(120), 0);
      body.removeView(status);
      fullscreen.addView(status, statusPos);
      a.setContentView(fullscreen);
      androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(
          fullscreen,
          (v, insets) -> {
            var ime = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.ime());
            v.setPadding(0, 0, 0, ime.bottom);
            return insets;
          });
    } else {
      dialog.setContentView(body);
      dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED);
      dialog.setOnDismissListener(
          v -> {
            active.remove(a);
            shutdown();
          });
      dialog.show();
      dialog.getWindow().setLayout(-1, -1);
      dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    }
    reader.submit(this::connect);
  }

  private void button(LinearLayout row, String label, Runnable action) {
    MaterialButton b = new MaterialButton(themed);
    b.setAllCaps(false);
    b.setText(label);
    b.setPadding(dp(6), 0, dp(6), 0);
    b.setTextSize(12);
    row.addView(b, new LinearLayout.LayoutParams(0, -2, 1));
    b.setOnClickListener(v -> action.run());
  }

  private int dp(int value) {
    return Math.round(value * activity.getResources().getDisplayMetrics().density);
  }

  private void pointer(MotionEvent event, int buttons, boolean release) {
    if (protocol == null
        || fbWidth < 1
        || fbHeight < 1
        || view.getWidth() < 1
        || view.getHeight() < 1) return;
    float scale = Math.min((float) view.getWidth() / fbWidth, (float) view.getHeight() / fbHeight);
    int x = (int) ((event.getX() - (view.getWidth() - fbWidth * scale) / 2) / scale),
        y = (int) ((event.getY() - (view.getHeight() - fbHeight * scale) / 2) / scale);
    if (!release && (x < 0 || y < 0 || x >= fbWidth || y >= fbHeight)) return;
    lastX = Math.max(0, Math.min(fbWidth - 1, x));
    lastY = Math.max(0, Math.min(fbHeight - 1, y));
    int px = lastX, py = lastY;
    send(() -> protocol.pointer(px, py, buttons));
  }

  private void wheel(float axis, int positive, int negative, int buttons) {
    if (!Float.isFinite(axis) || axis == 0) return;
    int steps = Math.min(8, Math.max(1, (int) Math.ceil(Math.abs(axis)))),
        bit = axis > 0 ? positive : negative,
        x = lastX,
        y = lastY;
    send(
        () -> {
          for (int i = 0; i < steps; i++) {
            protocol.pointer(x, y, buttons | bit);
            protocol.pointer(x, y, buttons);
          }
        });
  }

  private boolean physicalKey(int code, KeyEvent event) {
    if (PhysicalInput.hostKey(code) || protocol == null) return false;
    long id = ((long) event.getDeviceId() << 32) | (code & 0xffffffffL);
    if (event.getAction() == KeyEvent.ACTION_UP) {
      Integer symbol = heldKeys.remove(id);
      if (symbol == null) return false;
      if (symbol != 0) send(() -> protocol.key(symbol, false));
      return true;
    }
    if (event.getAction() != KeyEvent.ACTION_DOWN) return false;
    int unicode = event.getUnicodeChar(PhysicalInput.unicodeMeta(event.getMetaState()));
    if ((unicode & KeyCharacterMap.COMBINING_ACCENT) != 0) {
      deadAccent = unicode & KeyCharacterMap.COMBINING_ACCENT_MASK;
      heldKeys.put(id, 0);
      return true;
    }
    if (deadAccent != 0 && unicode > 0) {
      int composed = KeyCharacterMap.getDeadChar(deadAccent, unicode);
      int accent = deadAccent;
      deadAccent = 0;
      if (composed != 0) unicode = composed;
      else {
        int symbol = PhysicalInput.symbol(0, accent);
        if (symbol != 0)
          send(
              () -> {
                protocol.key(symbol, true);
                protocol.key(symbol, false);
              });
      }
    }
    int symbol = PhysicalInput.symbol(code, unicode);
    if (symbol == 0) return false;
    heldKeys.put(id, symbol);
    send(() -> protocol.key(symbol, true));
    return true;
  }

  private void releaseInputs() {
    if (protocol == null) return;
    List<Integer> keys = new ArrayList<>(heldKeys.values());
    heldKeys.clear();
    deadAccent = 0;
    int x = lastX, y = lastY;
    send(
        () -> {
          for (int symbol : keys) if (symbol != 0) protocol.key(symbol, false);
          protocol.pointer(x, y, 0);
        });
  }

  private interface Input {
    void run() throws IOException;
  }

  private void send(Input action) {
    if (closed || failedState.get() || protocol == null) return;
    try {
      writer.submit(
          () -> {
            if (!closed)
              try {
                action.run();
              } catch (IOException error) {
                failed(error);
              }
          });
    } catch (RejectedExecutionException full) {
      failed(new IOException("Input queue full"));
    }
  }

  private void key(int symbol) {
    send(
        () -> {
          RawRfb p = protocol;
          p.key(symbol, true);
          p.key(symbol, false);
        });
  }

  private void text(EditText draft, boolean enter) {
    String text = draft.getText().toString();
    if (text.length() > 8192) {
      status.setText(tr("Input is too long", "入力が長すぎます"));
      return;
    }
    draft.setText("");
    send(
        () -> {
          RawRfb p = protocol;
          for (int offset = 0; offset < text.length(); ) {
            int cp = text.codePointAt(offset);
            offset += Character.charCount(cp);
            int symbol = cp == 10 ? 0xff0d : cp > 255 ? 0x01000000 | cp : cp;
            p.key(symbol, true);
            p.key(symbol, false);
          }
          if (enter) {
            p.key(0xff0d, true);
            p.key(0xff0d, false);
          }
        });
  }

  private void connect() {
    try {
      if (!RuntimePreferences.configReady())
        throw new IOException("Vector configuration unavailable");
      LocalSocket current = new LocalSocket();
      socket = current;
      current.connect(
          new LocalSocketAddress(
              "net.ryuya_dev.protectedlinux.console", LocalSocketAddress.Namespace.ABSTRACT));
      if (current.getPeerCredentials().getUid() != 0)
        throw new IOException("Untrusted VM launcher");
      current.setSoTimeout(600000);
      current.getOutputStream().write('V');
      int result = current.getInputStream().read();
      if (result != 0)
        throw new IOException(
            result == 2
                ? "Run plb-install-gui inside Linux first"
                : "VM display unavailable (" + result + ")");
      current.setSoTimeout(0);
      RawRfb.Wire wire =
          new RawRfb.Wire() {
            public int read(ByteBuffer data) throws IOException {
              for (; ; )
                try {
                  return Os.read(current.getFileDescriptor(), data);
                } catch (ErrnoException error) {
                  if (error.errno != OsConstants.EINTR) throw new IOException(error);
                }
            }

            public synchronized void send(byte[] bytes) throws IOException {
              current.getOutputStream().write(bytes);
            }
          };
      RawRfb p =
          new RawRfb(
              wire,
              new RawRfb.Sink() {
                public void size(int w, int h) {
                  fbWidth = w;
                  fbHeight = h;
                }

                public void desktopResizeAvailable() {
                  requestRotation();
                }

                public void desktopResizeRejected() {
                  activity.runOnUiThread(
                      () -> {
                        if (!closed)
                          status.setText(
                              tr(
                                  "Resolution change was rejected; keeping the current size.",
                                  "解像度変更が拒否されたため、現在のサイズを維持します。"));
                      });
                }

                public void frame(ByteBuffer pixels, List<RawRfb.Rect> damage) throws IOException {
                  long started = System.nanoTime();
                  Upload u = new Upload(pixels, damage, fbWidth, fbHeight, new CountDownLatch(1));
                  if (closed) throw new IOException("Display closed");
                  pending.set(u);
                  view.requestRender();
                  try {
                    if (!awaitUpload(u) || closed)
                      throw new IOException("GPU display did not consume the frame");
                    long remaining = 1_000_000_000L / targetFps - (System.nanoTime() - started);
                    if (remaining > 0) TimeUnit.NANOSECONDS.sleep(remaining);
                  } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IOException("Display interrupted");
                  }
                }
              });
      p.handshake();
      protocol = p;
      if (activity instanceof DesktopActivity desktop) desktop.displayReady();
      android.util.Log.i("ProtectedLinux", "DISPLAY_TRANSPORT raw-gles");
      while (!closed) {
        p.update();
        if (!closed) SystemClock.sleep(1);
      }
    } catch (Exception error) {
      if (!closed) failed(error);
    }
  }

  private void requestRotation() {
    if (closed || !RuntimePreferences.followRotation() || protocol == null) return;
    boolean landscape =
        activity.getResources().getConfiguration().orientation
            == android.content.res.Configuration.ORIENTATION_LANDSCAPE;
    int[] size = DisplaySizing.dimensions(RuntimePreferences.resolution(), landscape);
    if (rotationWidth == size[0] && rotationHeight == size[1]) return;
    send(
        () -> {
          if (protocol.resizeDesktop(size[0], size[1])) {
            rotationWidth = size[0];
            rotationHeight = size[1];
            android.util.Log.i(
                "ProtectedLinux", "DISPLAY_RESIZE_REQUEST " + size[0] + "x" + size[1]);
          }
        });
  }

  private boolean awaitUpload(Upload upload) throws InterruptedException {
    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
    while (!upload.done.await(1, TimeUnit.SECONDS)) {
      if (closed) return false;
      if (paused) deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
      else if (System.nanoTime() > deadline) return false;
    }
    return true;
  }

  private void failed(Exception error) {
    if (closed || !failedState.compareAndSet(false, true)) return;
    reader.shutdownNow();
    writer.shutdownNow();
    Upload waiting = pending.getAndSet(null);
    if (waiting != null) waiting.done.countDown();
    android.util.Log.w("ProtectedLinux", "RAW_GPU_FAILED " + error.getClass().getSimpleName());
    LocalSocket current = socket;
    if (current != null)
      try {
        current.shutdownInput();
        current.shutdownOutput();
        current.close();
      } catch (IOException ignored) {
      }
    activity.runOnUiThread(
        () -> {
          if (!closed)
            status.setText(
                tr("Display stopped. Use compatibility display. ", "画面が停止しました。互換表示をお試しください。 ")
                    + error.getMessage());
        });
  }

  private void shutdown() {
    closed = true;
    Upload u = pending.getAndSet(null);
    if (u != null) u.done.countDown();
    LocalSocket current = socket;
    if (current != null) {
      try {
        current.shutdownInput();
        current.shutdownOutput();
        current.close();
      } catch (IOException ignored) {
      }
    }
    reader.shutdownNow();
    writer.shutdownNow();
    if (view != null) view.onPause();
  }

  private final class Renderer implements GLSurfaceView.Renderer {
    private int program, texture, vao, tw, th, sw, sh;
    private boolean fresh = true, logged;
    private final FloatBuffer vertices =
        ByteBuffer.allocateDirect(64)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(new float[] {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0});

    private int shader(int kind, String source) {
      int s = GLES30.glCreateShader(kind);
      GLES30.glShaderSource(s, source);
      GLES30.glCompileShader(s);
      int[] ok = new int[1];
      GLES30.glGetShaderiv(s, GLES30.GL_COMPILE_STATUS, ok, 0);
      if (ok[0] == 0) throw new IllegalStateException("GPU shader failed");
      return s;
    }

    public void onSurfaceCreated(GL10 unused, EGLConfig config) {
      try {
        program = GLES30.glCreateProgram();
        int vs =
            shader(
                GLES30.GL_VERTEX_SHADER,
                "#version 300 es\n"
                    + "layout(location=0) in vec2 p;layout(location=1) in vec2 t;out vec2 uv;void"
                    + " main(){gl_Position=vec4(p,0,1);uv=t;}");
        int fs =
            shader(
                GLES30.GL_FRAGMENT_SHADER,
                "#version 300 es\n"
                    + "precision mediump float;in vec2 uv;uniform sampler2D image;out vec4"
                    + " color;void main(){color=vec4(texture(image,uv).rgb,1.0);}");
        GLES30.glAttachShader(program, vs);
        GLES30.glAttachShader(program, fs);
        GLES30.glLinkProgram(program);
        GLES30.glDeleteShader(vs);
        GLES30.glDeleteShader(fs);
        int[] ok = new int[1];
        GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, ok, 0);
        if (ok[0] == 0) throw new IllegalStateException("GPU program failed");
        int[] names = new int[1];
        GLES30.glGenTextures(1, names, 0);
        texture = names[0];
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture);
        GLES30.glTexParameteri(
            GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR);
        GLES30.glTexParameteri(
            GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR);
        GLES30.glTexParameteri(
            GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE);
        GLES30.glTexParameteri(
            GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE);
        fresh = true;
        view.getHolder()
            .getSurface()
            .setFrameRate(targetFps, android.view.Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        int[] array = new int[1], buffer = new int[1];
        GLES30.glGenVertexArrays(1, array, 0);
        vao = array[0];
        GLES30.glBindVertexArray(vao);
        GLES30.glGenBuffers(1, buffer, 0);
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, buffer[0]);
        vertices.position(0);
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, 64, vertices, GLES30.GL_STATIC_DRAW);
        GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 16, 0);
        GLES30.glEnableVertexAttribArray(0);
        GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, 16, 8);
        GLES30.glEnableVertexAttribArray(1);
        android.util.Log.i(
            "ProtectedLinux", "RAW_GPU_READY renderer=" + GLES30.glGetString(GLES30.GL_RENDERER));
        if (protocol != null) send(() -> protocol.request(false));
      } catch (RuntimeException error) {
        failed(error);
      }
    }

    public void onSurfaceChanged(GL10 unused, int w, int h) {
      sw = w;
      sh = h;
      requestRotation();
    }

    public void onDrawFrame(GL10 unused) {
      if (failedState.get()) return;
      Upload u = pending.getAndSet(null);
      try {
        GLES30.glViewport(0, 0, sw, sh);
        GLES30.glClearColor(.06f, .05f, .07f, 1);
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT);
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture);
        if (u != null) {
          ByteBuffer data = u.pixels.duplicate();
          data.position(0);
          GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 4);
          if (fresh || tw != u.width || th != u.height) {
            tw = u.width;
            th = u.height;
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ROW_LENGTH, 0);
            GLES30.glTexImage2D(
                GLES30.GL_TEXTURE_2D,
                0,
                GLES30.GL_RGBA,
                tw,
                th,
                0,
                GLES30.GL_RGBA,
                GLES30.GL_UNSIGNED_BYTE,
                data);
            fresh = false;
          } else {
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ROW_LENGTH, tw);
            for (RawRfb.Rect r : u.damage) {
              data.position((r.y() * tw + r.x()) * 4);
              GLES30.glTexSubImage2D(
                  GLES30.GL_TEXTURE_2D,
                  0,
                  r.x(),
                  r.y(),
                  r.width(),
                  r.height(),
                  GLES30.GL_RGBA,
                  GLES30.GL_UNSIGNED_BYTE,
                  data);
            }
          }
          GLES30.glPixelStorei(GLES30.GL_UNPACK_ROW_LENGTH, 0);
          if (GLES30.glGetError() != GLES30.GL_NO_ERROR)
            throw new IllegalStateException("GPU upload failed");
          if (!logged) {
            logged = true;
            android.util.Log.i("ProtectedLinux", "RAW_GPU_FRAME " + tw + "x" + th);
            activity.runOnUiThread(
                () -> {
                  if (!closed) status.setText(tr("GPU display · raw pixels", "GPU表示 · 画素を直接転送"));
                });
          }
        }
        if (tw > 0 && th > 0) {
          float scale = Math.min((float) sw / tw, (float) sh / th);
          int w = Math.max(1, (int) (tw * scale)), h = Math.max(1, (int) (th * scale));
          GLES30.glViewport((sw - w) / 2, (sh - h) / 2, w, h);
          GLES30.glUseProgram(program);
          GLES30.glBindVertexArray(vao);
          GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4);
          if (GLES30.glGetError() != GLES30.GL_NO_ERROR)
            throw new IllegalStateException("GPU draw failed");
        }
      } catch (RuntimeException error) {
        failed(error);
      } finally {
        if (u != null) u.done.countDown();
      }
    }
  }
}
