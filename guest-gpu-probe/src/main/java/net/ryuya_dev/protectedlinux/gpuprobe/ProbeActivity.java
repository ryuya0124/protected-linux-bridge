package net.ryuya_dev.protectedlinux.gpuprobe;

import android.app.Activity;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.WindowInsets;
import android.view.WindowManager;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import org.json.JSONObject;

/** Bounded, guest-only GLES drawing/readback. Never constructs EGL on the Galaxy host. */
public final class ProbeActivity extends Activity {
  private static final String TAG = "PLBGpuProbe";
  private GLSurfaceView surface;
  private volatile boolean reported;
  private final Handler handler = new Handler(Looper.getMainLooper());

  @Override
  public void onCreate(Bundle state) {
    super.onCreate(state);
    String nonce = getIntent().getStringExtra("plb_nonce");
    if (!ProbeIdentity.allowed(Build.DEVICE, Build.HARDWARE, Build.VERSION.INCREMENTAL, nonce)) {
      Log.e(TAG, "PLB_GPU_PROBE_REFUSED guest_identity_or_nonce");
      finish();
      return;
    }
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    getWindow().setDecorFitsSystemWindows(false);
    surface = new GLSurfaceView(this);
    surface.setEGLContextClientVersion(2);
    surface.setEGLConfigChooser(8, 8, 8, 8, 0, 0);
    surface.setRenderer(
        new PatternRenderer(nonce, getIntent().getBooleanExtra("plb_motion", false)));
    setContentView(surface);
    surface.post(
        () -> {
          android.view.WindowInsetsController controller = surface.getWindowInsetsController();
          if (controller != null) controller.hide(WindowInsets.Type.systemBars());
        });
    // GPU work stops after the two readback phases. This bounds even a failed launch.
    handler.postDelayed(
        () -> {
          if (!reported) Log.e(TAG, "PLB_GPU_PROBE_TIMEOUT nonce=" + nonce);
          finish();
        },
        20000);
  }

  private int inputSamples;

  @Override
  public boolean dispatchTouchEvent(android.view.MotionEvent event) {
    if (getIntent().getBooleanExtra("plb_input", false)) {
      if (inputSamples++ < 256)
        Log.i(
            TAG,
            "PLB_GPU_INPUT nonce="
                + getIntent().getStringExtra("plb_nonce")
                + " action="
                + event.getActionMasked()
                + " pointers="
                + event.getPointerCount()
                + " device="
                + event.getDeviceId());
      return true;
    }
    return super.dispatchTouchEvent(event);
  }

  @Override
  public void onPause() {
    if (surface != null) surface.onPause();
    super.onPause();
  }

  @Override
  public void onResume() {
    super.onResume();
    if (surface != null) surface.onResume();
  }

  @Override
  public void onDestroy() {
    handler.removeCallbacksAndMessages(null);
    super.onDestroy();
  }

  private final class PatternRenderer implements GLSurfaceView.Renderer {
    private final String nonce;
    private final boolean motion;
    private long measuredAt;
    private int measuredFrames;
    private final FloatBuffer vertices =
        ByteBuffer.allocateDirect(24)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(new float[] {-1, -1, 3, -1, -1, 3});
    private final ByteBuffer pixel = ByteBuffer.allocateDirect(4);
    private int program;
    private int width;
    private int height;
    private int frame;
    private boolean firstReadback;
    private boolean secondReadback;
    private boolean stopped;
    private String vendor;
    private String renderer;
    private String version;

    PatternRenderer(String nonce, boolean motion) {
      this.nonce = nonce;
      this.motion = motion;
      vertices.position(0);
    }

    @Override
    public void onSurfaceCreated(GL10 unused, EGLConfig config) {
      try {
        vendor = GLES20.glGetString(GLES20.GL_VENDOR);
        renderer = GLES20.glGetString(GLES20.GL_RENDERER);
        version = GLES20.glGetString(GLES20.GL_VERSION);
        int vertexShader = shader(GLES20.GL_VERTEX_SHADER, source("pattern.vert"));
        int fragmentShader = shader(GLES20.GL_FRAGMENT_SHADER, source("pattern.frag"));
        program = GLES20.glCreateProgram();
        GLES20.glAttachShader(program, vertexShader);
        GLES20.glAttachShader(program, fragmentShader);
        GLES20.glBindAttribLocation(program, 0, "position");
        GLES20.glLinkProgram(program);
        int[] okay = new int[1];
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, okay, 0);
        GLES20.glDeleteShader(vertexShader);
        GLES20.glDeleteShader(fragmentShader);
        if (okay[0] != GLES20.GL_TRUE)
          throw new IllegalStateException(
              "program link: " + bounded(GLES20.glGetProgramInfoLog(program)));
        GLES20.glDisable(GLES20.GL_DITHER);
        GLES20.glDisable(GLES20.GL_BLEND);
        GLES20.glDisable(GLES20.GL_DEPTH_TEST);
        int[] texture = new int[1];
        GLES20.glGenTextures(1, texture, 0);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture[0]);
        GLES20.glTexParameteri(
            GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST);
        GLES20.glTexParameteri(
            GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST);
        GLES20.glTexParameteri(
            GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(
            GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
        ByteBuffer pixels = ByteBuffer.allocateDirect(32 * 4);
        for (int index = 0; index < nonce.length(); index++) {
          int value = Character.digit(nonce.charAt(index), 16) * 17;
          pixels.put((byte) value).put((byte) (255 - value)).put((byte) 127).put((byte) 255);
        }
        pixels.position(0);
        GLES20.glTexImage2D(
            GLES20.GL_TEXTURE_2D,
            0,
            GLES20.GL_RGBA,
            32,
            1,
            0,
            GLES20.GL_RGBA,
            GLES20.GL_UNSIGNED_BYTE,
            pixels);
      } catch (RuntimeException error) {
        report(false, "initialization: " + error.getMessage());
      }
    }

    private String source(String name) {
      try (java.io.InputStream input = getAssets().open(name)) {
        byte[] bytes = input.readNBytes(4097);
        if (bytes.length > 4096) throw new IllegalStateException("shader size");
        return new String(bytes, java.nio.charset.StandardCharsets.UTF_8);
      } catch (java.io.IOException error) {
        throw new IllegalStateException("shader asset", error);
      }
    }

    private int shader(int kind, String source) {
      int shader = GLES20.glCreateShader(kind);
      GLES20.glShaderSource(shader, source);
      GLES20.glCompileShader(shader);
      int[] okay = new int[1];
      GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, okay, 0);
      if (okay[0] != GLES20.GL_TRUE) {
        String reason = bounded(GLES20.glGetShaderInfoLog(shader));
        GLES20.glDeleteShader(shader);
        throw new IllegalStateException("shader compile: " + reason);
      }
      return shader;
    }

    @Override
    public void onSurfaceChanged(GL10 unused, int newWidth, int newHeight) {
      width = newWidth;
      height = newHeight;
      GLES20.glViewport(0, 0, width, height);
    }

    @Override
    public void onDrawFrame(GL10 unused) {
      if (stopped) return;
      try {
        // Let the asynchronous system-bar inset change settle before sampling.
        // The 20-second Activity deadline also bounds a permanently wrong size.
        if (width != 480 || height != 800) return;
        frame++;
        int phase = frame < 12 ? 0 : 1;
        GLES20.glUseProgram(program);
        GLES20.glUniform2f(GLES20.glGetUniformLocation(program, "size"), width, height);
        float colorPhase = motion ? (float) (0.5 + 0.5 * Math.sin(frame * 0.05)) : phase;
        GLES20.glUniform1f(GLES20.glGetUniformLocation(program, "phase"), colorPhase);
        GLES20.glUniform1i(GLES20.glGetUniformLocation(program, "nonceTexture"), 0);
        GLES20.glVertexAttribPointer(0, 2, GLES20.GL_FLOAT, false, 0, vertices);
        GLES20.glEnableVertexAttribArray(0);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, 3);
        if (motion) {
          reported = true;
          measuredFrames++;
          long now = android.os.SystemClock.elapsedRealtime();
          if (measuredAt == 0) measuredAt = now;
          if (now - measuredAt >= 5000) {
            Log.i(
                TAG,
                "PLB_GPU_MOTION nonce="
                    + nonce
                    + " frames="
                    + measuredFrames
                    + " elapsed_ms="
                    + (now - measuredAt)
                    + " renderer="
                    + bounded(renderer));
            measuredAt = now;
            measuredFrames = 0;
          }
        }
        if (!motion && (frame == 2 || frame == 22)) {
          GLES20.glFinish();
          boolean okay = readback(phase);
          if (frame == 2) firstReadback = okay;
          else {
            secondReadback = okay;
            report(firstReadback && secondReadback, "two_phase_readback");
          }
        }
        if (GLES20.glGetError() != GLES20.GL_NO_ERROR) report(false, "gl_error");
      } catch (RuntimeException error) {
        report(false, "draw: " + error.getMessage());
      }
    }

    private boolean readback(int phase) {
      int[][] samples = {
        {width / 4, height / 4, phase == 0 ? 255 : 0, 0, phase == 0 ? 0 : 255},
        {width * 3 / 4, height / 4, 0, 255, 0},
        {width / 4, height * 3 / 4, phase == 0 ? 0 : 255, 0, phase == 0 ? 255 : 0},
        {width * 3 / 4, height * 3 / 4, 255, 255, 0},
        {width / 2, height / 2, 255, 255, 255}
      };
      for (int[] sample : samples) {
        if (!pixelMatches(sample[0], sample[1], sample[2], sample[3], sample[4])) return false;
      }
      for (int index = 0; index < nonce.length(); index++) {
        int value = Character.digit(nonce.charAt(index), 16) * 17;
        if (!pixelMatches((index * 2 + 1) * width / 64, height / 25, value, 255 - value, 127))
          return false;
      }
      return GLES20.glGetError() == GLES20.GL_NO_ERROR;
    }

    private boolean pixelMatches(int x, int y, int red, int green, int blue) {
      int[] expectedChannels = {red, green, blue, 255};
      pixel.position(0);
      GLES20.glReadPixels(x, y, 1, 1, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, pixel);
      for (int channel = 0; channel < 4; channel++) {
        if (Math.abs((pixel.get(channel) & 255) - expectedChannels[channel]) > 2) return false;
      }
      return true;
    }

    private void report(boolean okay, String reason) {
      if (stopped) return;
      stopped = true;
      reported = true;
      try {
        JSONObject result = new JSONObject();
        result.put("probe", "plb-gles-v1");
        result.put("nonce", nonce);
        result.put("vendor", bounded(vendor));
        result.put("renderer", bounded(renderer));
        result.put("version", bounded(version));
        result.put("width", width);
        result.put("height", height);
        result.put("frame", frame);
        result.put("readback_ok", okay);
        result.put("phase0_readback_ok", firstReadback);
        result.put("phase1_readback_ok", secondReadback);
        result.put("reason", reason);
        Log.i(TAG, "PLB_GPU_PROBE_RESULT " + result);
      } catch (org.json.JSONException error) {
        Log.e(TAG, "PLB_GPU_PROBE_RESULT_ENCODING_FAILED");
      }
      runOnUiThread(() -> surface.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY));
    }

    private String bounded(String text) {
      return text == null ? "" : text.substring(0, Math.min(400, text.length()));
    }
  }
}
