package net.ryuya_dev.protectedlinux;

import android.os.Bundle;
import android.util.Base64;
import android.view.*;
import android.webkit.*;
import android.widget.*;
import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.*;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.tabs.TabLayout;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;

/** In-app guest PTYs. The only input destination is the authenticated guest channel. */
public final class StandaloneTerminalActivity extends AppCompatActivity {
  private final ExecutorService startup = Executors.newSingleThreadExecutor();
  private final List<Pane> panes = new ArrayList<>();
  private FrameLayout content;
  private TabLayout tabs;
  private int sequence;

  @Override
  public void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    EdgeToEdge.enable(this);
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.debian_terminal);
    toolbar.setNavigationIcon(R.drawable.ic_back);
    toolbar.setNavigationContentDescription(R.string.asset_back);
    toolbar.setNavigationOnClickListener(v -> finish());
    root.addView(toolbar);
    toolbar
        .getMenu()
        .add(0, 1, 0, R.string.console_add)
        .setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM);
    toolbar
        .getMenu()
        .add(0, 2, 1, R.string.console_close)
        .setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER);
    toolbar
        .getMenu()
        .add(0, 3, 2, R.string.open_fullscreen)
        .setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER);
    toolbar.setOnMenuItemClickListener(
        item -> {
          if (item.getItemId() == 1) add();
          else if (item.getItemId() == 2) remove();
          else if (item.getItemId() == 3)
            startActivity(new android.content.Intent(this, DesktopActivity.class));
          return true;
        });
    tabs = new TabLayout(this);
    tabs.setTabMode(TabLayout.MODE_SCROLLABLE);
    root.addView(tabs);
    content = new FrameLayout(this);
    root.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));
    setContentView(root);
    ViewCompat.setOnApplyWindowInsetsListener(
        root,
        (view, insets) -> {
          var bars =
              insets.getInsets(
                  WindowInsetsCompat.Type.systemBars()
                      | WindowInsetsCompat.Type.displayCutout()
                      | WindowInsetsCompat.Type.ime());
          view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
          return insets;
        });
    tabs.addOnTabSelectedListener(
        new TabLayout.OnTabSelectedListener() {
          public void onTabSelected(TabLayout.Tab tab) {
            select(tab);
          }

          public void onTabUnselected(TabLayout.Tab tab) {}

          public void onTabReselected(TabLayout.Tab tab) {
            select(tab);
          }
        });
    add();
  }

  private void select(TabLayout.Tab tab) {
    if (tab == null) return;
    for (Pane pane : panes) pane.web.setVisibility(pane == tab.getTag() ? View.VISIBLE : View.GONE);
    if (tab.getTag() instanceof Pane pane) pane.web.requestFocus();
  }

  private void add() {
    if (panes.size() >= 8) {
      Toast.makeText(this, R.string.console_limit, Toast.LENGTH_SHORT).show();
      return;
    }
    try {
      Pane pane = new Pane();
      panes.add(pane);
      content.addView(pane.web, new FrameLayout.LayoutParams(-1, -1));
      tabs.addTab(
          tabs.newTab().setText(getString(R.string.console_tab, ++sequence)).setTag(pane), true);
    } catch (IOException failure) {
      Toast.makeText(this, R.string.console_failed, Toast.LENGTH_LONG).show();
    }
  }

  private void remove() {
    int index = tabs.getSelectedTabPosition();
    if (index >= 0 && tabs.getTabAt(index).getTag() instanceof Pane pane) remove(pane);
  }

  private void remove(Pane pane) {
    TabLayout.Tab selected = null;
    for (int i = 0; i < tabs.getTabCount(); i++) {
      TabLayout.Tab tab = tabs.getTabAt(i);
      if (tab != null && tab.getTag() == pane) {
        selected = tab;
        break;
      }
    }
    panes.remove(pane);
    content.removeView(pane.web);
    pane.close();
    if (selected != null) tabs.removeTab(selected);
    if (panes.isEmpty()) finish();
  }

  @Override
  protected void onDestroy() {
    for (Pane pane : panes) pane.close();
    panes.clear();
    startup.shutdownNow();
    super.onDestroy();
  }

  private String asset(String name) throws IOException {
    try (InputStream input = getAssets().open("web/" + name)) {
      return new String(input.readAllBytes(), StandardCharsets.UTF_8);
    }
  }

  private final class Pane {
    final WebView web = new WebView(StandaloneTerminalActivity.this);
    final Semaphore pending = new Semaphore(8);
    volatile boolean closed;
    private VmChannel channel;
    private boolean requested;

    Pane() throws IOException {
      String html =
          asset("terminal.html")
              .replace("/* PLB_XTERM_CSS */", asset("xterm.css"))
              .replace("/* PLB_XTERM_JS */", asset("xterm.js"));
      web.setBackgroundColor(0xff101014);
      web.getSettings().setJavaScriptEnabled(true);
      web.getSettings().setAllowFileAccess(false);
      web.getSettings().setAllowContentAccess(false);
      web.getSettings().setBlockNetworkLoads(true);
      web.addJavascriptInterface(new ConsoleApi(), "ProtectedConsole");
      web.setWebViewClient(
          new WebViewClient() {
            @Override
            public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
              if (!closed) {
                Toast.makeText(
                        StandaloneTerminalActivity.this, R.string.console_failed, Toast.LENGTH_LONG)
                    .show();
                remove(Pane.this);
              }
              return true;
            }

            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
              return true;
            }

            public WebResourceResponse shouldInterceptRequest(
                WebView view, WebResourceRequest request) {
              return new WebResourceResponse(
                  "text/plain", "UTF-8", new ByteArrayInputStream(new byte[0]));
            }
          });
      web.loadDataWithBaseURL("https://protected-linux.invalid/", html, "text/html", "UTF-8", null);
    }

    private void emit(byte[] bytes) throws InterruptedException {
      if (closed) return;
      pending.acquire();
      String script = "acceptBytes('" + Base64.encodeToString(bytes, Base64.NO_WRAP) + "')";
      if (!web.post(
          () -> {
            if (closed) pending.release();
            else web.evaluateJavascript(script, result -> pending.release());
          })) pending.release();
    }

    private void message(String text) {
      try {
        emit(("\r\n[" + text + "]\r\n").getBytes(StandardCharsets.UTF_8));
      } catch (InterruptedException interrupted) {
        Thread.currentThread().interrupt();
      }
    }

    private synchronized void connect() {
      if (closed || requested) return;
      requested = true;
      startup.submit(
          () -> {
            message(getString(R.string.console_connecting));
            String result = RuntimeSetup.start(getApplicationContext());
            synchronized (Pane.this) {
              if (closed) return;
              if (!result.equals("ready")) {
                message(getString(RuntimeSetup.message(result)));
                return;
              }
              channel =
                  new VmChannel(
                      new VmChannel.Listener() {
                        public void bytes(byte[] data) throws InterruptedException {
                          emit(data);
                        }

                        public void ended(String error) {
                          message(error == null ? getString(R.string.console_ended) : error);
                        }
                      });
              channel.open('R');
            }
          });
    }

    synchronized void close() {
      if (closed) return;
      closed = true;
      if (channel != null) channel.close();
      pending.release(8);
      web.removeJavascriptInterface("ProtectedConsole");
      web.stopLoading();
      web.destroy();
    }

    public final class ConsoleApi {
      @JavascriptInterface
      public void ready() {
        connect();
      }

      @JavascriptInterface
      public void write(String data) {
        if (data == null || data.length() > 65536) return;
        synchronized (Pane.this) {
          if (!closed && channel != null) channel.write(data.getBytes(StandardCharsets.UTF_8));
        }
      }
    }
  }
}
