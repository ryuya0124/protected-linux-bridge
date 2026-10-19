package net.ryuya_dev.protectedlinux;

import android.content.*;
import android.content.res.ColorStateList;
import android.os.*;
import android.view.*;
import android.widget.*;
import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.*;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.color.MaterialColors;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.materialswitch.MaterialSwitch;
import com.google.android.material.progressindicator.CircularProgressIndicator;
import com.google.android.material.textfield.MaterialAutoCompleteTextView;
import com.google.android.material.textfield.TextInputLayout;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;

public final class MainActivity extends AppCompatActivity {
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private StatusCard runtime, framework;
  private MaterialButton prepare, open;
  private boolean preparing;
  private TextView kernelStatus;
  private MaterialAutoCompleteTextView networkSource;
  private int networkApp;
  private MaterialSwitch networkToggle;
  private TextView networkStatus;
  private MaterialButton networkSave;
  private final androidx.activity.result.ActivityResultLauncher<String[]> kernelImporter =
      registerForActivityResult(
          new androidx.activity.result.contract.ActivityResultContracts.OpenDocument(),
          uri -> {
            if (uri != null) {
              kernelStatus.setText(R.string.checking);
              worker.submit(
                  () -> {
                    boolean ok = KernelProfiles.install(this, uri);
                    runOnUiThread(
                        () -> {
                          if (!isDestroyed())
                            kernelStatus.setText(
                                ok ? R.string.kernel_imported : R.string.kernel_import_failed);
                        });
                  });
            }
          });
  private Runnable refreshAudioDevices;
  private android.media.AudioDeviceCallback deviceCallback;
  private TextView clipboardStatus, microphoneStatus;
  private MaterialSwitch microphoneToggle;
  private final android.content.SharedPreferences.OnSharedPreferenceChangeListener
      preferencesChanged =
          (p, key) -> {
            if (key != null && key.startsWith("microphone_"))
              runOnUiThread(this::updateMicrophoneState);
          };
  private int resolutionId = 2, displayFps = 30;
  private MaterialAutoCompleteTextView resolutionMenu, fpsMenu;
  private static final int[] FPS = {15, 24, 30, 60, 90, 120};
  private MaterialSwitch gpuLinux, gpuDesktop, gpuTransfer;
  private TextView graphicsStatus;
  private MaterialButton graphicsSave, graphicsRestart;

  private BridgeApplication app() {
    return (BridgeApplication) getApplication();
  }

  @Override
  public void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    EdgeToEdge.enable(this);
    LinearLayout root = column();
    FrameLayout content = new FrameLayout(this);
    root.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));
    com.google.android.material.bottomnavigation.BottomNavigationView navigation =
        new com.google.android.material.bottomnavigation.BottomNavigationView(this);
    navigation.setLabelVisibilityMode(
        com.google.android.material.navigation.NavigationBarView.LABEL_VISIBILITY_LABELED);
    root.addView(navigation, new LinearLayout.LayoutParams(-1, -2));
    int[] labels = {
      R.string.tab_home, R.string.tab_display, R.string.tab_integration, R.string.tab_settings
    };
    int[] icons = {
      R.drawable.ic_terminal, R.drawable.ic_display, R.drawable.ic_link, R.drawable.ic_settings
    };
    LinearLayout[] pages = new LinearLayout[4];
    for (int n = 0; n < 4; n++) {
      ScrollView scroll = new ScrollView(this);
      scroll.setFillViewport(true);
      pages[n] = column();
      pages[n].setPadding(dp(20), dp(12), dp(20), dp(28));
      scroll.addView(pages[n]);
      content.addView(scroll, new FrameLayout.LayoutParams(-1, -1));
      scroll.setVisibility(n == 0 ? View.VISIBLE : View.GONE);
      navigation.getMenu().add(0, 100 + n, n, labels[n]).setIcon(icons[n]);
    }
    navigation.setOnItemSelectedListener(
        item -> {
          selectedTab = item.getItemId() - 100;
          for (int n = 0; n < 4; n++)
            content.getChildAt(n).setVisibility(n == selectedTab ? View.VISIBLE : View.GONE);
          return true;
        });
    setContentView(root);
    ViewCompat.setOnApplyWindowInsetsListener(
        root,
        (v, insets) -> {
          var bars =
              insets.getInsets(
                  WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout());
          v.setPadding(bars.left, bars.top, bars.right, 0);
          return insets;
        });
    LinearLayout page = pages[0];
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.app_name);
    toolbar.setLogo(R.drawable.ic_terminal);
    page.addView(toolbar);
    page.addView(text(getString(R.string.intro), 20));
    page.addView(text(getString(R.string.experimental), 13));
    runtime = new StatusCard(page, R.string.root_title);
    framework = new StatusCard(page, R.string.framework_title);
    MaterialCardView toggleCard = card(page);
    LinearLayout toggleBody = column();
    toggleBody.setPadding(dp(16), dp(8), dp(16), dp(8));
    toggleCard.addView(toggleBody);
    MaterialSwitch toggle = new MaterialSwitch(this);
    toggle.setText(R.string.enabled);
    toggle.setChecked(getSharedPreferences("config", 0).getBoolean("enabled", true));
    toggleBody.addView(toggle);
    toggleBody.addView(text(getString(R.string.enabled_detail), 13));
    pages[1].addView(text(getString(R.string.tab_display), 24));
    graphicsCard(pages[1]);
    pages[2].addView(text(getString(R.string.tab_integration), 24));
    networkCard(pages[2]);
    integrationCard(pages[2]);
    audioDevicesCard(pages[2]);
    pages[3].addView(text(getString(R.string.tab_settings), 24));
    androidGuestCard(pages[3]);
    kernelCard(pages[3]);
    resourcesCard(pages[3]);
    storageCard(pages[3]);
    button(R.string.enable_terminal, page).setOnClickListener(v -> setupTerminal(false));
    button(R.string.install_linux, page).setOnClickListener(v -> setupTerminal(true));
    button(R.string.open_fullscreen, page).setOnClickListener(v -> openDesktop());
    button(R.string.open_fullscreen, pages[1]).setOnClickListener(v -> openDesktop());
    open = button(R.string.open_terminal, page);
    open.setOnClickListener(
        v -> {
          try {
            startActivity(
                new Intent()
                    .setComponent(
                        new ComponentName(RuntimeSetup.HOST, RuntimeSetup.HOST + ".MainActivity")));
          } catch (Exception error) {
            runtime.show(0, R.string.terminal_missing);
          }
        });
    prepare = button(R.string.prepare, page);
    prepare.setOnClickListener(v -> startRuntime());
    button(R.string.stop_vm, page)
        .setOnClickListener(
            v ->
                new MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.stop_vm)
                    .setMessage(R.string.stop_detail)
                    .setNegativeButton(android.R.string.cancel, null)
                    .setPositiveButton(
                        android.R.string.ok,
                        (d, w) -> {
                          runtime.show(-1, R.string.checking);
                          worker.submit(
                              () -> {
                                boolean ok = RuntimeSetup.stop(this);
                                runOnUiThread(
                                    () -> {
                                      if (!isDestroyed())
                                        runtime.show(
                                            ok ? 1 : 0,
                                            ok ? R.string.runtime_stopped : R.string.stop_failed);
                                    });
                              });
                        })
                    .show());
    toggle.setOnCheckedChangeListener(
        (v, on) -> {
          getSharedPreferences("config", 0).edit().putBoolean("enabled", on).apply();
          app().publish();
          open.setEnabled(on);
          if (on) startRuntime();
          else {
            getSharedPreferences("config", 0)
                .edit()
                .putBoolean("microphone_enabled", false)
                .apply();
            MicrophoneService.apply(this);
            worker.submit(() -> RuntimeSetup.clipboard(this, 0));
            runtime.show(1, R.string.runtime_disabled);
          }
        });
    open.setEnabled(toggle.isChecked());
    page = pages[3];
    info(page, R.string.setup_title, R.string.setup);
    info(page, R.string.limits_title, R.string.limits);
    info(page, R.string.network_title, R.string.network_detail);
    info(page, R.string.tools_title, R.string.tools_detail);
    info(page, R.string.gpu_title, R.string.gpu_detail);
    info(page, R.string.safety_title, R.string.safety);
    info(page, R.string.protection_title, R.string.host_protection);
    button(R.string.licenses, page).setOnClickListener(v -> licenses());
    navigation.setSelectedItemId(100 + (saved == null ? 0 : saved.getInt("selectedTab", 0)));
    startRuntime();
  }

  @Override
  protected void onResume() {
    super.onResume();
    getSharedPreferences("config", 0).registerOnSharedPreferenceChangeListener(preferencesChanged);
    updateMicrophoneState();
    if (deviceCallback == null) {
      deviceCallback =
          new android.media.AudioDeviceCallback() {
            @Override
            public void onAudioDevicesAdded(android.media.AudioDeviceInfo[] devices) {
              refreshDevices();
            }

            @Override
            public void onAudioDevicesRemoved(android.media.AudioDeviceInfo[] devices) {
              refreshDevices();
            }
          };
      getSystemService(android.media.AudioManager.class)
          .registerAudioDeviceCallback(deviceCallback, new Handler(getMainLooper()));
    }
    refreshDevices();
    app().observer = this::refreshFramework;
    refreshFramework();
    if (microphoneToggle != null)
      microphoneToggle.setChecked(
          getSharedPreferences("config", 0).getBoolean("microphone_enabled", false)
              && checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                  == android.content.pm.PackageManager.PERMISSION_GRANTED);
  }

  @Override
  protected void onPause() {
    if (deviceCallback != null) {
      getSystemService(android.media.AudioManager.class)
          .unregisterAudioDeviceCallback(deviceCallback);
      deviceCallback = null;
    }
    getSharedPreferences("config", 0)
        .unregisterOnSharedPreferenceChangeListener(preferencesChanged);
    app().observer = null;
    super.onPause();
  }

  @Override
  protected void onDestroy() {
    worker.shutdownNow();
    super.onDestroy();
  }

  private void refreshFramework() {
    if (framework == null) return;
    int state = app().frameworkState();
    framework.show(
        state == 3 ? 2 : state == 2 ? 1 : 0,
        switch (state) {
          case 1 -> R.string.scope_missing;
          case 2 -> R.string.scope_ready;
          case 3 -> R.string.module_loaded;
          default -> R.string.framework_unavailable;
        });
  }

  private void startRuntime() {
    if (preparing) return;
    preparing = true;
    prepare.setEnabled(false);
    runtime.show(-1, R.string.requesting_root);
    worker.submit(
        () -> {
          String result = RuntimeSetup.start(this);
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                preparing = false;
                prepare.setEnabled(true);
                runtime.show(
                    "ready".equals(result) ? 2 : "disabled".equals(result) ? 1 : 0,
                    "ready".equals(result)
                        ? R.string.runtime_ready
                        : "disabled".equals(result)
                            ? R.string.runtime_disabled
                            : "unsupported_architecture".equals(result)
                                ? R.string.unsupported_architecture
                                : "unsupported_version".equals(result)
                                    ? R.string.unsupported_version
                                    : R.string.runtime_unavailable);
                refreshFramework();
                if ("ready".equals(result)) {
                  loadGraphics();
                  loadResources();
                  loadStorage();
                  loadNetwork();
                  applyClipboard();
                }
              });
        });
  }

  private void setupTerminal(boolean installer) {
    runtime.show(-1, R.string.setup_running);
    worker.submit(
        () -> {
          boolean ok = TerminalSetup.run(installer);
          runOnUiThread(
              () -> {
                if (!isDestroyed()) {
                  runtime.show(ok ? 2 : 0, ok ? R.string.setup_done : R.string.setup_failed);
                  if (ok && !installer) startRuntime();
                }
              });
        });
  }

  private void openDesktop() {
    runtime.show(-1, R.string.checking);
    worker.submit(
        () -> {
          String result = RuntimeSetup.start(this);
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                if ("ready".equals(result)) {
                  runtime.show(2, R.string.runtime_ready);
                  startActivity(new Intent(this, DesktopActivity.class));
                } else runtime.show(0, R.string.runtime_unavailable);
              });
        });
  }

  private void graphicsCard(LinearLayout page) {
    MaterialCardView c = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(10), dp(18), dp(12));
    c.addView(body);
    body.addView(text(getString(R.string.graphics_controls), 20));
    gpuLinux = new MaterialSwitch(this);
    gpuLinux.setText(R.string.gpu_linux);
    body.addView(gpuLinux);
    gpuDesktop = new MaterialSwitch(this);
    gpuDesktop.setText(R.string.gpu_desktop);
    body.addView(gpuDesktop);
    gpuTransfer = new MaterialSwitch(this);
    gpuTransfer.setText(R.string.gpu_transfer);
    body.addView(gpuTransfer);
    gpuTransfer.setChecked(getSharedPreferences("config", 0).getBoolean("gpu_display", false));
    gpuLinux.setOnCheckedChangeListener(
        (v, on) -> {
          if (!on) gpuDesktop.setChecked(false);
        });
    gpuDesktop.setOnCheckedChangeListener(
        (v, on) -> {
          if (on) gpuLinux.setChecked(true);
        });
    resolutionMenu = selection(body, R.string.resolution_label, R.array.resolutions);
    resolutionMenu.setText(getResources().getStringArray(R.array.resolutions)[resolutionId], false);
    resolutionMenu.setOnItemClickListener((parent, selected, index, id) -> resolutionId = index);
    fpsMenu = selection(body, R.string.fps_label, R.array.frame_rates);
    fpsMenu.setText("30 fps", false);
    fpsMenu.setOnItemClickListener((parent, selected, index, id) -> displayFps = FPS[index]);
    MaterialSwitch follow = new MaterialSwitch(this);
    follow.setText(R.string.follow_rotation);
    follow.setChecked(getSharedPreferences("config", 0).getBoolean("follow_rotation", false));
    body.addView(follow);
    follow.setOnCheckedChangeListener(
        (v, on) -> {
          getSharedPreferences("config", 0).edit().putBoolean("follow_rotation", on).apply();
          app().publish();
        });
    MaterialAutoCompleteTextView orientation =
        selection(body, R.string.desktop_orientation, R.array.desktop_orientations);
    orientation.setText(
        getResources()
            .getStringArray(R.array.desktop_orientations)[
            Math.max(
                0,
                Math.min(2, getSharedPreferences("config", 0).getInt("desktop_orientation", 0)))],
        false);
    orientation.setOnItemClickListener(
        (p, v, n, id) ->
            getSharedPreferences("config", 0).edit().putInt("desktop_orientation", n).apply());
    body.addView(text(getString(R.string.graphics_hint), 13));
    graphicsStatus = text(getString(R.string.checking), 14);
    body.addView(graphicsStatus);
    gpuTransfer.setOnCheckedChangeListener(
        (v, on) -> {
          getSharedPreferences("config", 0).edit().putBoolean("gpu_display", on).apply();
          app().publish();
          graphicsStatus.setText(R.string.graphics_view_saved);
        });
    button(R.string.gpu_light_preset, body)
        .setOnClickListener(
            v ->
                new MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.gpu_light_preset)
                    .setMessage(R.string.gpu_light_detail)
                    .setNegativeButton(android.R.string.cancel, null)
                    .setPositiveButton(android.R.string.ok, (d, w) -> lightGpuPreset())
                    .show());
    graphicsSave = button(R.string.graphics_save, body);
    graphicsSave.setOnClickListener(v -> saveGraphics(false));
    graphicsRestart = button(R.string.graphics_restart, body);
    graphicsRestart.setOnClickListener(
        v ->
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.graphics_restart)
                .setMessage(R.string.graphics_restart_detail)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(android.R.string.ok, (d, w) -> saveGraphics(true))
                .show());
  }

  private void lightGpuPreset() {
    graphicsStatus.setText(R.string.checking);
    worker.submit(
        () -> {
          var r = RuntimeSetup.resources(this);
          boolean ok =
              r != null
                  && RuntimeSetup.saveResources(
                      this,
                      new RuntimeSetup.Resources(
                          512, 1, r.minutes(), r.disk(), r.keepAlive(), r.systemd()))
                  && RuntimeSetup.saveGraphics(this, 1, 0, 30);
          if (ok) {
            getSharedPreferences("config", 0)
                .edit()
                .putBoolean("gpu_display", true)
                .putInt("resolution_id", 0)
                .putInt("display_fps", 30)
                .apply();
            app().publish();
            ok = RuntimeSetup.stop(this) && "ready".equals(RuntimeSetup.start(this));
          }
          boolean success = ok;
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                gpuTransfer.setChecked(true);
                loadGraphics();
                loadResources();
                graphicsStatus.setText(
                    success ? R.string.graphics_applied : R.string.graphics_failed);
              });
        });
  }

  private void networkCard(LinearLayout page) {
    MaterialCardView card = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(10), dp(18), dp(12));
    card.addView(body);
    body.addView(text(getString(R.string.tun_title), 20));
    body.addView(text(getString(R.string.tun_summary), 13));
    networkToggle = new MaterialSwitch(this);
    networkToggle.setText(R.string.tun_enabled);
    body.addView(networkToggle);
    networkSource = selection(body, R.string.tun_source, R.array.network_sources);
    networkSource.setText(getResources().getStringArray(R.array.network_sources)[0], false);
    networkSource.setOnItemClickListener((p, v, n, id) -> networkApp = n);
    networkStatus = text(getString(R.string.checking), 14);
    body.addView(networkStatus);
    info(body, R.string.network_title, R.string.tun_detail);
    button(R.string.tun_refresh, body).setOnClickListener(v -> loadNetwork());
    networkSave = button(R.string.tun_apply, body);
    networkSave.setOnClickListener(
        v ->
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.tun_apply)
                .setMessage(R.string.tun_confirm)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                    android.R.string.ok,
                    (d, w) -> {
                      boolean enabled = networkToggle.isChecked();
                      int source = networkApp;
                      networkSave.setEnabled(false);
                      networkStatus.setText(R.string.checking);
                      worker.submit(
                          () -> {
                            boolean ok =
                                RuntimeSetup.saveNetwork(this, enabled, source)
                                    && RuntimeSetup.stop(this)
                                    && "ready".equals(RuntimeSetup.start(this));
                            runOnUiThread(
                                () -> {
                                  if (isDestroyed()) return;
                                  networkSave.setEnabled(true);
                                  if (ok) loadNetwork();
                                  else networkStatus.setText(R.string.graphics_failed);
                                });
                          });
                    })
                .show());
  }

  private void loadNetwork() {
    if (networkToggle == null) return;
    worker.submit(
        () -> {
          int saved = RuntimeSetup.networkValue(this, false),
              live = RuntimeSetup.networkValue(this, true),
              source = RuntimeSetup.networkApp(this);
          boolean custom = getSharedPreferences("config", 0).getBoolean("mainline_kernel", false);
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                networkApp = source == 1 ? 1 : 0;
                networkSource.setText(
                    getResources().getStringArray(R.array.network_sources)[networkApp], false);
                networkSource.setEnabled(custom && source >= 0);
                networkToggle.setEnabled(custom && saved >= 0);
                networkSave.setEnabled(custom && saved >= 0);
                networkToggle.setChecked(custom && saved == 1);
                networkStatus.setText(
                    !custom
                        ? R.string.tun_stock
                        : live < 0
                            ? R.string.tun_query_failed
                            : live == 3
                                ? R.string.tun_ready
                                : live == 2
                                    ? R.string.tun_failed
                                    : live == 1 ? R.string.tun_off : R.string.tun_waiting);
              });
        });
  }

  private MaterialAutoCompleteTextView selection(LinearLayout body, int label, int items) {
    TextInputLayout field = new TextInputLayout(this);
    field.setBoxBackgroundMode(TextInputLayout.BOX_BACKGROUND_OUTLINE);
    field.setEndIconMode(TextInputLayout.END_ICON_DROPDOWN_MENU);
    field.setHint(label);
    MaterialAutoCompleteTextView menu = new MaterialAutoCompleteTextView(field.getContext());
    menu.setInputType(0);
    var popup =
        com.google.android.material.shape.MaterialShapeDrawable.createWithElevationOverlay(
            this, dp(8));
    popup.setCornerSize(dp(12));
    popup.setFillColor(
        ColorStateList.valueOf(
            MaterialColors.getColor(
                this, com.google.android.material.R.attr.colorSurfaceContainerHigh, 0xff2b2930)));
    menu.setDropDownBackgroundDrawable(popup);
    menu.setOnClickListener(v -> menu.showDropDown());
    menu.setSimpleItems(getResources().getStringArray(items));
    field.addView(menu, new LinearLayout.LayoutParams(-1, -2));
    LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2);
    p.setMargins(0, dp(8), 0, dp(8));
    body.addView(field, p);
    return menu;
  }

  private void loadGraphics() {
    worker.submit(
        () -> {
          var state = RuntimeSetup.graphicsState(this);
          int mode = state.mode();
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                if (mode >= 0) {
                  gpuLinux.setChecked((mode & 1) != 0);
                  gpuDesktop.setChecked((mode & 2) != 0);
                }
                resolutionId = state.resolution();
                displayFps = state.fps();
                if (mode >= -1) {
                  getSharedPreferences("config", 0)
                      .edit()
                      .putInt("resolution_id", resolutionId)
                      .putInt("display_fps", displayFps)
                      .apply();
                  app().publish();
                }
                resolutionMenu.setText(
                    getResources().getStringArray(R.array.resolutions)[resolutionId], false);
                fpsMenu.setText(displayFps + " fps", false);
                graphicsStatus.setText(
                    mode == -1
                        ? R.string.graphics_existing
                        : mode == -2 ? R.string.graphics_failed : R.string.graphics_saved_state);
              });
        });
  }

  private void saveGraphics(boolean restart) {
    int mode = gpuLinux.isChecked() ? (gpuDesktop.isChecked() ? 3 : 1) : 0;
    int resolution = resolutionId, fps = displayFps;
    boolean transfer = gpuTransfer.isChecked();
    graphicsSave.setEnabled(false);
    graphicsRestart.setEnabled(false);
    graphicsStatus.setText(R.string.checking);
    worker.submit(
        () -> {
          boolean ok = RuntimeSetup.saveGraphics(this, mode, resolution, fps);
          if (ok) {
            getSharedPreferences("config", 0)
                .edit()
                .putBoolean("gpu_display", transfer)
                .putInt("display_fps", fps)
                .putInt("resolution_id", resolution)
                .apply();
            app().publish();
          }
          if (ok && restart)
            ok =
                RuntimeSetup.stop(this)
                    && "ready".equals(RuntimeSetup.start(this))
                    && RuntimeSetup.reopenTerminal();
          boolean success = ok;
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                graphicsSave.setEnabled(true);
                graphicsRestart.setEnabled(true);
                graphicsStatus.setText(
                    success
                        ? (restart ? R.string.graphics_applied : R.string.graphics_saved)
                        : R.string.graphics_failed);
              });
        });
  }

  private void kernelCard(LinearLayout page) {
    MaterialCardView c = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(10), dp(18), dp(12));
    c.addView(body);
    body.addView(text(getString(R.string.kernel_title), 20));
    body.addView(text(getString(R.string.kernel_detail), 13));
    MaterialAutoCompleteTextView mode =
        selection(body, R.string.kernel_profile, R.array.kernel_profiles);
    mode.setText(
        getResources()
            .getStringArray(R.array.kernel_profiles)[
            getSharedPreferences("config", 0).getBoolean("mainline_kernel", false) ? 1 : 0],
        false);
    final boolean[] selected = {
      getSharedPreferences("config", 0).getBoolean("mainline_kernel", false)
    };
    mode.setOnItemClickListener((p, v, n, id) -> selected[0] = n == 1);
    kernelStatus =
        text(
            getString(
                getSharedPreferences("config", 0).getBoolean("mainline_kernel", false)
                    ? R.string.kernel_mainline_state
                    : R.string.kernel_stock_state),
            14);
    body.addView(kernelStatus);
    button(R.string.kernel_import, body)
        .setOnClickListener(v -> kernelImporter.launch(new String[] {"*/*"}));
    MaterialButton apply = button(R.string.kernel_apply, body);
    apply.setOnClickListener(
        v ->
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.kernel_apply)
                .setMessage(R.string.kernel_confirm)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                    android.R.string.ok,
                    (d, w) -> {
                      apply.setEnabled(false);
                      kernelStatus.setText(R.string.kernel_switching);
                      worker.submit(
                          () -> {
                            boolean ok = KernelProfiles.select(this, selected[0]);
                            runOnUiThread(
                                () -> {
                                  if (isDestroyed()) return;
                                  apply.setEnabled(true);
                                  kernelStatus.setText(
                                      !ok
                                          ? R.string.kernel_switch_failed
                                          : selected[0]
                                              ? R.string.kernel_mainline_state
                                              : R.string.kernel_stock_state);
                                  loadGraphics();
                                  loadResources();
                                  loadNetwork();
                                });
                          });
                    })
                .show());
  }

  private final int[] RAM = {512, 1024, 2048, 4096, 8192},
      CORES = {1, 2, 4, 8},
      MINUTES = {15, 30, 60, 120, 360, 0},
      DISKS = {0, 4, 8, 16, 32};
  private TextView storageStatus;
  private com.google.android.material.textfield.TextInputEditText storageSize;
  private final java.util.ArrayList<MaterialButton> storageButtons = new java.util.ArrayList<>();
  private boolean storageBusy;

  private static String capacity(long bytes) {
    return String.format(java.util.Locale.getDefault(), "%.2f GiB", bytes / 1073741824.0);
  }

  private void storageCard(LinearLayout page) {
    MaterialCardView c = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(10), dp(18), dp(12));
    c.addView(body);
    body.addView(text(getString(R.string.storage_title), 20));
    storageStatus = text(getString(R.string.checking), 14);
    storageStatus.setTextIsSelectable(true);
    body.addView(storageStatus);
    MaterialButton refresh = button(R.string.storage_refresh, body);
    storageButtons.add(refresh);
    refresh.setOnClickListener(v -> loadStorage());
    TextInputLayout field = new TextInputLayout(this);
    field.setHint(getString(R.string.storage_capacity));
    storageSize = new com.google.android.material.textfield.TextInputEditText(field.getContext());
    storageSize.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
    field.addView(storageSize);
    body.addView(field);
    int[] labels = {
      R.string.storage_resize,
      R.string.storage_compact,
      R.string.storage_restore,
      R.string.storage_discard
    };
    String[] actions = {"resize", "compact", "restore", "discard"};
    for (int i = 0; i < labels.length; i++) {
      String action = actions[i];
      MaterialButton b = button(labels[i], body);
      storageButtons.add(b);
      b.setOnClickListener(
          v -> {
            int gib = 0;
            if (action.equals("resize")) {
              try {
                gib = Integer.parseInt(String.valueOf(storageSize.getText()));
                if (gib < 1 || gib > 128) throw new NumberFormatException();
              } catch (NumberFormatException e) {
                field.setError(getString(R.string.storage_invalid));
                return;
              }
            }
            field.setError(null);
            int target = gib;
            new MaterialAlertDialogBuilder(this)
                .setTitle(b.getText())
                .setMessage(
                    action.equals("discard")
                        ? R.string.storage_discard_warning
                        : R.string.storage_warning)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(android.R.string.ok, (d, w) -> runStorage(action, target))
                .show();
          });
    }
    MaterialButton cancel = button(R.string.storage_cancel, body);
    cancel.setOnClickListener(v -> RuntimeSetup.cancelStorage(this));
    body.addView(text(getString(R.string.storage_detail), 13));
  }

  private void loadStorage() {
    worker.submit(
        () -> {
          var s = RuntimeSetup.storage(this);
          runOnUiThread(
              () -> {
                if (isDestroyed() || storageBusy) return;
                if (s == null) {
                  storageStatus.setText(R.string.storage_unavailable);
                  return;
                }
                String message =
                    getString(
                        R.string.storage_metrics,
                        capacity(s.capacity()),
                        capacity(s.allocated()),
                        capacity(s.backupCapacity()),
                        capacity(s.backupAllocated()),
                        capacity(s.hostFree()));
                message +=
                    "\n"
                        + (s.guestTotal() > 0
                            ? getString(
                                R.string.storage_guest_metrics,
                                capacity(s.guestTotal()),
                                capacity(s.guestUsed()),
                                capacity(s.guestFree()))
                            : getString(R.string.storage_guest_offline));
                storageStatus.setText(message);
              });
        });
  }

  private void runStorage(String action, int gib) {
    if (storageBusy) return;
    storageBusy = true;
    storageButtons.forEach(b -> b.setEnabled(false));
    storageStatus.setText(R.string.storage_running);
    worker.submit(
        () -> {
          int result =
              RuntimeSetup.storageJob(
                  this,
                  action,
                  gib,
                  line ->
                      runOnUiThread(
                          () -> {
                            if (!isDestroyed())
                              storageStatus.setText(
                                  getString(R.string.storage_running)
                                      + "\n"
                                      + storageProgress(line));
                          }));
          boolean prepared = "ready".equals(RuntimeSetup.start(this));
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                runtime.show(
                    prepared ? 2 : 0,
                    prepared ? R.string.runtime_ready : R.string.runtime_unavailable);
                storageBusy = false;
                storageButtons.forEach(b -> b.setEnabled(true));
                storageStatus.setText(
                    result == 0 && prepared
                        ? R.string.storage_done
                        : result == 28
                            ? R.string.storage_locked
                            : result == 40
                                ? R.string.storage_backup_exists
                                : result == 41
                                    ? R.string.storage_no_space
                                    : R.string.storage_failed);
                loadResources();
              });
        });
  }

  private String storageProgress(String line) {
    return getString(
        line.contains("FSCK")
            ? R.string.storage_checking
            : line.contains("RESIZE")
                ? R.string.storage_resizing
                : line.contains("ZERO_FREE")
                    ? R.string.storage_zeroing
                    : line.contains("COPY_FINAL")
                        ? R.string.storage_verifying
                        : R.string.storage_copying);
  }

  private int resourceRam = 512, resourceCpus = 1, resourceMinutes = 30, resourceDisk = 0;
  private MaterialAutoCompleteTextView ramMenu, cpuMenu, minutesMenu, diskMenu;
  private MaterialSwitch keepVm, systemd;
  private TextView resourceStatus;

  private void androidGuestCard(LinearLayout page) {
    MaterialCardView card = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(14), dp(18), dp(14));
    card.addView(body);
    body.addView(text(getString(R.string.android_guest_title), 20));
    body.addView(text(getString(R.string.android_guest_description), 14));
    button(R.string.android_guest_open, body)
        .setOnClickListener(
            view -> {
              int[] selectedMemory = {1536};
              LinearLayout options = column();
              options.setPadding(dp(24), dp(4), dp(24), 0);
              options.addView(text(getString(R.string.android_guest_launch_conditions), 14));
              MaterialAutoCompleteTextView memory =
                  selection(
                      options, R.string.android_guest_memory_label, R.array.android_guest_memories);
              memory.setText(
                  getResources().getStringArray(R.array.android_guest_memories)[0], false);
              TextView gpuConditions = text(getString(R.string.android_guest_gpu_conditions), 14);
              gpuConditions.setVisibility(View.GONE);
              options.addView(gpuConditions);
              memory.setOnItemClickListener(
                  (parent, item, position, id) -> {
                    selectedMemory[0] = AndroidGuestProfile.memoryAt(position);
                    gpuConditions.setVisibility(
                        selectedMemory[0] == 4096 ? View.VISIBLE : View.GONE);
                  });
              android.widget.ScrollView scroll = new android.widget.ScrollView(this);
              scroll.addView(options);
              new MaterialAlertDialogBuilder(this)
                  .setTitle(R.string.android_guest_title)
                  .setView(scroll)
                  .setNegativeButton(android.R.string.cancel, null)
                  .setPositiveButton(
                      R.string.android_guest_open,
                      (dialog, which) -> confirmAndroidGuest(selectedMemory[0]))
                  .show();
            });
  }

  private void confirmAndroidGuest(int memoryMib) {
    Runnable launch =
        () ->
            startActivity(
                new Intent(this, AndroidGuestActivity.class)
                    .putExtra(AndroidGuestActivity.EXTRA_MEMORY_MIB, memoryMib)
                    .putExtra(
                        AndroidGuestActivity.EXTRA_POLICY_ACKNOWLEDGED,
                        AndroidGuestProfile.guarded(memoryMib)));
    if (memoryMib != 4096) {
      launch.run();
      return;
    }
    new MaterialAlertDialogBuilder(this)
        .setTitle(R.string.android_guest_gpu_title)
        .setMessage(R.string.android_guest_gpu_conditions)
        .setNegativeButton(android.R.string.cancel, null)
        .setPositiveButton(R.string.android_guest_open, (dialog, which) -> launch.run())
        .show();
  }

  private void resourcesCard(LinearLayout page) {
    MaterialCardView card = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(10), dp(18), dp(12));
    card.addView(body);
    body.addView(text(getString(R.string.resources_title), 20));
    ramMenu = selection(body, R.string.memory_label, R.array.memories);
    ramMenu.setText(getResources().getStringArray(R.array.memories)[0], false);
    ramMenu.setOnItemClickListener((p, v, i, id) -> resourceRam = RAM[i]);
    cpuMenu = selection(body, R.string.cpu_label, R.array.cpus);
    cpuMenu.setText("1", false);
    cpuMenu.setOnItemClickListener((p, v, i, id) -> resourceCpus = CORES[i]);
    minutesMenu = selection(body, R.string.lifetime_label, R.array.lifetimes);
    minutesMenu.setText(getResources().getStringArray(R.array.lifetimes)[1], false);
    minutesMenu.setOnItemClickListener((p, v, i, id) -> resourceMinutes = MINUTES[i]);
    diskMenu = selection(body, R.string.disk_label, R.array.disks);
    diskMenu.setText(getResources().getStringArray(R.array.disks)[0], false);
    diskMenu.setOnItemClickListener((p, v, n, id) -> resourceDisk = DISKS[n]);
    keepVm = new MaterialSwitch(this);
    keepVm.setText(R.string.keep_vm);
    body.addView(keepVm);
    systemd = new MaterialSwitch(this);
    systemd.setText(R.string.systemd);
    body.addView(systemd);
    body.addView(text(getString(R.string.resources_hint), 13));
    resourceStatus = text(getString(R.string.checking), 14);
    body.addView(resourceStatus);
    button(R.string.resources_save, body).setOnClickListener(v -> saveResources(false));
    button(R.string.resources_restart, body)
        .setOnClickListener(
            v ->
                new MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.resources_restart)
                    .setMessage(R.string.resources_restart_detail)
                    .setNegativeButton(android.R.string.cancel, null)
                    .setPositiveButton(android.R.string.ok, (d, w) -> saveResources(true))
                    .show());
  }

  private static int index(int[] values, int selected) {
    for (int i = 0; i < values.length; i++) if (values[i] == selected) return i;
    return 0;
  }

  private void loadResources() {
    worker.submit(
        () -> {
          var r = RuntimeSetup.resources(this);
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                if (r == null) {
                  resourceStatus.setText(R.string.graphics_failed);
                  return;
                }
                resourceRam = r.memory();
                resourceCpus = r.cpus();
                resourceMinutes = r.minutes();
                resourceDisk = r.disk();
                ramMenu.setText(
                    getResources().getStringArray(R.array.memories)[index(RAM, resourceRam)],
                    false);
                cpuMenu.setText("" + resourceCpus, false);
                minutesMenu.setText(
                    getResources()
                        .getStringArray(R.array.lifetimes)[index(MINUTES, resourceMinutes)],
                    false);
                diskMenu.setText(
                    resourceDisk != 0 && index(DISKS, resourceDisk) == 0
                        ? resourceDisk + " GiB"
                        : getResources().getStringArray(R.array.disks)[index(DISKS, resourceDisk)],
                    false);
                keepVm.setChecked(r.keepAlive());
                systemd.setChecked(r.systemd());
                resourceStatus.setText(R.string.graphics_saved_state);
              });
        });
  }

  private void saveResources(boolean restart) {
    var r =
        new RuntimeSetup.Resources(
            resourceRam,
            resourceCpus,
            resourceMinutes,
            resourceDisk,
            keepVm.isChecked(),
            systemd.isChecked());
    resourceStatus.setText(R.string.checking);
    worker.submit(
        () -> {
          boolean ok = RuntimeSetup.saveResources(this, r);
          if (ok && restart)
            ok =
                RuntimeSetup.stop(this)
                    && "ready".equals(RuntimeSetup.start(this))
                    && RuntimeSetup.reopenTerminal();
          boolean success = ok;
          runOnUiThread(
              () -> {
                if (!isDestroyed())
                  resourceStatus.setText(
                      success
                          ? (restart ? R.string.graphics_applied : R.string.graphics_saved)
                          : R.string.graphics_failed);
              });
        });
  }

  private void integrationCard(LinearLayout page) {
    MaterialCardView c = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(10), dp(18), dp(12));
    c.addView(body);
    body.addView(text(getString(R.string.integration_title), 20));
    body.addView(text(getString(R.string.integration_hint), 13));
    MaterialSwitch speaker = new MaterialSwitch(this);
    speaker.setText(R.string.speaker);
    speaker.setChecked(getSharedPreferences("config", 0).getBoolean("speaker_enabled", true));
    body.addView(speaker);
    speaker.setOnCheckedChangeListener(
        (v, on) ->
            getSharedPreferences("config", 0).edit().putBoolean("speaker_enabled", on).apply());
    MaterialSwitch mic = new MaterialSwitch(this);
    microphoneToggle = mic;
    mic.setText(R.string.microphone);
    mic.setChecked(
        getSharedPreferences("config", 0).getBoolean("microphone_enabled", false)
            && checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                == android.content.pm.PackageManager.PERMISSION_GRANTED);
    body.addView(mic);
    mic.setOnCheckedChangeListener(
        (v, on) -> {
          if (on
              && checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                  != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            mic.setChecked(false);
            requestPermissions(new String[] {android.Manifest.permission.RECORD_AUDIO}, 22);
            return;
          }
          getSharedPreferences("config", 0).edit().putBoolean("microphone_enabled", on).apply();
          MicrophoneService.apply(this);
          updateMicrophoneState();
        });
    microphoneStatus = text("", 14);
    body.addView(microphoneStatus);
    updateMicrophoneState();
    MaterialAutoCompleteTextView microphoneMode =
        selection(body, R.string.microphone_mode, R.array.microphone_modes);
    microphoneMode.setText(
        getResources()
            .getStringArray(R.array.microphone_modes)[
            getSharedPreferences("config", 0).getBoolean("microphone_background", false) ? 1 : 0],
        false);
    microphoneMode.setOnItemClickListener(
        (p, v, i, id) -> {
          getSharedPreferences("config", 0)
              .edit()
              .putBoolean("microphone_background", i == 1)
              .apply();
          MicrophoneService.apply(this);
          updateMicrophoneState();
          if (i == 1
              && mic.isChecked()
              && checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                  != android.content.pm.PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] {android.Manifest.permission.POST_NOTIFICATIONS}, 23);
        });
    body.addView(text(getString(R.string.clipboard_auto_hint), 13));
    MaterialSwitch automatic = new MaterialSwitch(this);
    automatic.setText(R.string.clipboard_auto);
    automatic.setChecked(getSharedPreferences("config", 0).getBoolean("clipboard_auto", false));
    body.addView(automatic);
    MaterialSwitch toLinux = new MaterialSwitch(this);
    toLinux.setText(R.string.clipboard_to_linux_auto);
    toLinux.setChecked(getSharedPreferences("config", 0).getBoolean("clipboard_to_linux", true));
    body.addView(toLinux);
    MaterialSwitch toAndroid = new MaterialSwitch(this);
    toAndroid.setText(R.string.clipboard_to_android_auto);
    toAndroid.setChecked(
        getSharedPreferences("config", 0).getBoolean("clipboard_to_android", true));
    body.addView(toAndroid);
    automatic.setOnCheckedChangeListener(
        (v, on) -> {
          getSharedPreferences("config", 0).edit().putBoolean("clipboard_auto", on).apply();
          applyClipboard();
        });
    toLinux.setOnCheckedChangeListener(
        (v, on) -> {
          getSharedPreferences("config", 0).edit().putBoolean("clipboard_to_linux", on).apply();
          applyClipboard();
        });
    toAndroid.setOnCheckedChangeListener(
        (v, on) -> {
          getSharedPreferences("config", 0).edit().putBoolean("clipboard_to_android", on).apply();
          applyClipboard();
        });
    clipboardStatus = text("", 14);
    body.addView(clipboardStatus);
    button(R.string.clip_to_linux, body)
        .setOnClickListener(v -> ClipboardBridge.transfer(this, true, clipboardStatus::setText));
    button(R.string.clip_to_android, body)
        .setOnClickListener(v -> ClipboardBridge.transfer(this, false, clipboardStatus::setText));
  }

  private void audioDevicesCard(LinearLayout page) {
    MaterialCardView card = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(10), dp(18), dp(12));
    card.addView(body);
    body.addView(text(getString(R.string.audio_devices_title), 20));
    body.addView(text(getString(R.string.audio_devices_hint), 13));
    MaterialSwitch linux = new MaterialSwitch(this);
    linux.setText(R.string.audio_peripheral_linux);
    linux.setChecked(getSharedPreferences("config", 0).getBoolean("peripheral_audio_linux", false));
    body.addView(linux);
    linux.setOnCheckedChangeListener(
        (v, on) -> {
          getSharedPreferences("config", 0).edit().putBoolean("peripheral_audio_linux", on).apply();
          MicrophoneService.reconfigure(this);
        });
    LinearLayout menus = column();
    body.addView(menus);
    Runnable refresh =
        () -> {
          menus.removeAllViews();
          for (boolean input : new boolean[] {false, true}) {
            var choices = AudioDevices.choices(this, input);
            MaterialAutoCompleteTextView menu =
                selection(
                    menus,
                    input ? R.string.audio_input_device : R.string.audio_output_device,
                    R.array.audio_device_placeholder);
            menu.setSimpleItems(
                choices.stream().map(AudioDevices.Choice::label).toArray(String[]::new));
            String name = input ? "audio_input" : "audio_output",
                value =
                    getSharedPreferences("config", 0).getString(name, input ? "builtin" : "auto");
            String current =
                choices.stream()
                    .filter(c -> c.key().equals(value))
                    .map(AudioDevices.Choice::label)
                    .findFirst()
                    .orElse(getString(R.string.audio_device_missing));
            menu.setText(current, false);
            menu.setOnItemClickListener(
                (p, v, n, id) -> {
                  getSharedPreferences("config", 0)
                      .edit()
                      .putString(name, choices.get(n).key())
                      .apply();
                  MicrophoneService.reconfigure(this);
                });
          }
        };
    refreshAudioDevices = refresh;
    refresh.run();
    button(R.string.audio_refresh_devices, body).setOnClickListener(v -> refresh.run());
    body.addView(text(getString(R.string.usb_direct_unavailable), 13));
  }

  private void refreshDevices() {
    if (refreshAudioDevices != null && !isDestroyed()) refreshAudioDevices.run();
  }

  private void updateMicrophoneState() {
    if (microphoneStatus == null) return;
    var p = getSharedPreferences("config", 0);
    if (microphoneToggle != null
        && microphoneToggle.isChecked() != p.getBoolean("microphone_enabled", false))
      microphoneToggle.setChecked(p.getBoolean("microphone_enabled", false));
    int id =
        !p.getBoolean("microphone_enabled", false)
            ? R.string.microphone_off
            : !p.getBoolean("microphone_background", false)
                ? R.string.microphone_gui_wait
                : p.getInt("microphone_state", 0) == 3
                    ? R.string.microphone_start_failed
                    : !MicrophoneService.running
                        ? R.string.microphone_not_running
                        : p.getInt("microphone_state", 0) == 2
                            ? R.string.microphone_active
                            : R.string.checking;
    microphoneStatus.setText(id);
  }

  @Override
  public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
    super.onRequestPermissionsResult(request, permissions, results);
    if (request == 22 && results.length > 0) {
      if (results[0] == android.content.pm.PackageManager.PERMISSION_GRANTED
          && microphoneToggle != null) microphoneToggle.setChecked(true);
      else
        android.widget.Toast.makeText(
                this, R.string.microphone_permission_needed, android.widget.Toast.LENGTH_LONG)
            .show();
    }
  }

  private void applyClipboard() {
    var p = getSharedPreferences("config", 0);
    int mode =
        p.getBoolean("enabled", true) && p.getBoolean("clipboard_auto", false)
            ? (p.getBoolean("clipboard_to_linux", true) ? 1 : 0)
                | (p.getBoolean("clipboard_to_android", true) ? 2 : 0)
            : 0;
    if (clipboardStatus != null) clipboardStatus.setText(R.string.checking);
    worker.submit(
        () -> {
          boolean ok = RuntimeSetup.clipboard(this, mode);
          runOnUiThread(
              () -> {
                if (!isDestroyed() && clipboardStatus != null)
                  clipboardStatus.setText(
                      !ok
                          ? R.string.clipboard_auto_failed
                          : mode == 0 ? R.string.clipboard_manual : R.string.clipboard_auto_ready);
              });
        });
  }

  private int selectedTab;

  @Override
  protected void onSaveInstanceState(Bundle state) {
    super.onSaveInstanceState(state);
    state.putInt("selectedTab", selectedTab);
  }

  private int dp(int n) {
    return Math.round(n * getResources().getDisplayMetrics().density);
  }

  private LinearLayout column() {
    LinearLayout v = new LinearLayout(this);
    v.setOrientation(LinearLayout.VERTICAL);
    return v;
  }

  private TextView text(String value, int size) {
    TextView t = new TextView(this);
    t.setText(value);
    t.setTextSize(size);
    t.setPadding(0, dp(8), 0, dp(8));
    return t;
  }

  private MaterialCardView card(LinearLayout parent) {
    MaterialCardView c = new MaterialCardView(this);
    c.setRadius(dp(24));
    LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2);
    p.setMargins(0, dp(6), 0, dp(6));
    parent.addView(c, p);
    return c;
  }

  private MaterialButton button(int label, LinearLayout parent) {
    MaterialButton b = new MaterialButton(this);
    b.setText(label);
    LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2);
    p.setMargins(0, dp(4), 0, dp(4));
    parent.addView(b, p);
    return b;
  }

  private void info(LinearLayout parent, int title, int detail) {
    button(title, parent)
        .setOnClickListener(
            v -> {
              TextView t = text(getString(detail), 14);
              t.setPadding(dp(24), dp(8), dp(24), dp(8));
              ScrollView box = new ScrollView(this);
              box.addView(t);
              new MaterialAlertDialogBuilder(this)
                  .setTitle(title)
                  .setView(box)
                  .setPositiveButton(android.R.string.ok, null)
                  .show();
            });
  }

  private void licenses() {
    try (var own = getAssets().open("APP_LICENSE.txt");
        var other = getAssets().open("THIRD_PARTY_NOTICES.txt")) {
      TextView t =
          text(
              new String(own.readAllBytes(), StandardCharsets.UTF_8)
                  + "\n\n"
                  + new String(other.readAllBytes(), StandardCharsets.UTF_8),
              12);
      t.setTextIsSelectable(true);
      t.setPadding(dp(20), dp(8), dp(20), dp(8));
      ScrollView box = new ScrollView(this);
      box.addView(t);
      new MaterialAlertDialogBuilder(this)
          .setTitle(R.string.licenses)
          .setView(box)
          .setPositiveButton(android.R.string.ok, null)
          .show();
    } catch (IOException error) {
      android.util.Log.e("ProtectedLinux", "LICENSE_READ_FAILED");
    }
  }

  private final class StatusCard {
    private final TextView detail;
    private final ImageView icon;
    private final CircularProgressIndicator progress;

    StatusCard(LinearLayout parent, int label) {
      MaterialCardView c = card(parent);
      LinearLayout row = new LinearLayout(MainActivity.this);
      row.setGravity(Gravity.CENTER_VERTICAL);
      row.setPadding(dp(16), dp(10), dp(16), dp(10));
      c.addView(row);
      FrameLayout leading = new FrameLayout(MainActivity.this);
      row.addView(leading, new LinearLayout.LayoutParams(dp(32), dp(32)));
      icon = new ImageView(MainActivity.this);
      leading.addView(icon, new FrameLayout.LayoutParams(-1, -1));
      progress = new CircularProgressIndicator(MainActivity.this);
      progress.setIndicatorSize(dp(28));
      progress.setTrackThickness(dp(3));
      progress.setIndeterminate(true);
      leading.addView(progress, new FrameLayout.LayoutParams(-1, -1));
      LinearLayout copy = column();
      LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, -2, 1);
      p.setMarginStart(dp(16));
      row.addView(copy, p);
      copy.addView(text(getString(label), 18));
      detail = text(getString(R.string.checking), 14);
      copy.addView(detail);
      show(-1, R.string.checking);
    }

    void show(int state, int message) {
      detail.setText(message);
      progress.setVisibility(state < 0 ? View.VISIBLE : View.GONE);
      icon.setVisibility(state < 0 ? View.GONE : View.VISIBLE);
      icon.setImageResource(
          state == 2
              ? R.drawable.ic_check
              : state == 1 ? R.drawable.ic_wait : R.drawable.ic_warning);
      icon.setImageTintList(
          ColorStateList.valueOf(
              MaterialColors.getColor(
                  MainActivity.this,
                  state == 0
                      ? androidx.appcompat.R.attr.colorError
                      : androidx.appcompat.R.attr.colorPrimary,
                  0xff6750a4)));
      icon.setContentDescription(
          getString(
              state == 2
                  ? R.string.state_ready
                  : state == 1 ? R.string.state_waiting : R.string.state_attention));
    }
  }
}
