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

  private enum Detail {
    GRAPHICS(1, R.string.ui_debian_graphics),
    RESOURCES(1, R.string.ui_debian_resources),
    DISK(1, R.string.ui_debian_disk),
    KERNEL(1, R.string.ui_debian_kernel),
    NETWORK(2, R.string.ui_debian_network),
    AUDIO(2, R.string.ui_debian_audio),
    CLIPBOARD(2, R.string.ui_debian_clipboard),
    TERMINAL(3, R.string.ui_terminal_integration),
    ABOUT(3, R.string.ui_about);
    final int tab, title;

    Detail(int tab, int title) {
      this.tab = tab;
      this.title = title;
    }
  }

  private final java.util.Map<Detail, View> detailViews = new java.util.EnumMap<>(Detail.class);
  private final java.util.List<MaterialButton> sessionButtons = new java.util.ArrayList<>();
  private FrameLayout pageHost, detailHost;
  private LinearLayout rootView;
  private com.google.android.material.bottomnavigation.BottomNavigationView navigation;
  private MaterialSwitch connectionToggle;
  private Detail selectedDetail;
  private androidx.activity.OnBackPressedCallback detailBack;

  @Override
  public void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    EdgeToEdge.enable(this);
    rootView = column();
    PageMotion.configureActivity(this, rootView);
    pageHost = new FrameLayout(this);
    rootView.addView(pageHost, new LinearLayout.LayoutParams(-1, 0, 1));
    navigation = new com.google.android.material.bottomnavigation.BottomNavigationView(this);
    navigation.setLabelVisibilityMode(
        com.google.android.material.navigation.NavigationBarView.LABEL_VISIBILITY_LABELED);
    rootView.addView(navigation, new LinearLayout.LayoutParams(-1, -2));
    int[] labels = {
      R.string.ui_overview, R.string.ui_vm_tab, R.string.ui_devices_tab, R.string.tab_settings
    };
    int[] icons = {
      R.drawable.ic_console, R.drawable.ic_vm, R.drawable.ic_link, R.drawable.ic_settings
    };
    LinearLayout[] pages = new LinearLayout[4];
    for (int index = 0; index < 4; index++) {
      ScrollView scroll = new ScrollView(this);
      scroll.setFillViewport(true);
      pages[index] = column();
      pages[index].setPadding(dp(20), dp(8), dp(20), dp(28));
      scroll.addView(pages[index]);
      pageHost.addView(scroll, new FrameLayout.LayoutParams(-1, -1));
      scroll.setVisibility(index == 0 ? View.VISIBLE : View.GONE);
      navigation.getMenu().add(0, 100 + index, index, labels[index]).setIcon(icons[index]);
    }
    detailHost = new FrameLayout(this);
    detailHost.setVisibility(View.GONE);
    pageHost.addView(detailHost, new FrameLayout.LayoutParams(-1, -1));
    detailBack =
        new androidx.activity.OnBackPressedCallback(false) {
          @Override
          public void handleOnBackPressed() {
            closeDetail();
          }
        };
    getOnBackPressedDispatcher().addCallback(this, detailBack);
    navigation.setOnItemSelectedListener(
        item -> {
          selectTab(item.getItemId() - 100);
          return true;
        });
    setContentView(rootView);
    ViewCompat.setOnApplyWindowInsetsListener(
        rootView,
        (view, insets) -> {
          var bars =
              insets.getInsets(
                  WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout());
          view.setPadding(
              bars.left,
              bars.top,
              bars.right,
              navigation.getVisibility() == View.VISIBLE ? 0 : bars.bottom);
          return insets;
        });

    LinearLayout home = pages[0];
    UiKit.heading(home, R.string.app_name, 0);
    home.addView(
        text(
            getString(
                R.string.ui_host_context, android.os.Build.VERSION.RELEASE, android.os.Build.MODEL),
            14));
    runtime = new StatusCard(home, R.string.ui_debian_launcher);
    MaterialCardView session = card(home);
    LinearLayout sessionBody = column();
    sessionBody.setPadding(dp(20), dp(16), dp(20), dp(16));
    session.addView(sessionBody);
    sessionBody.addView(
        UiKit.text(
            this,
            getText(R.string.ui_debian_name),
            com.google.android.material.R.style.TextAppearance_Material3_HeadlineSmall));
    sessionBody.addView(text(getString(R.string.ui_debian_session), 14));
    LinearLayout launch = UiKit.actions(sessionBody);
    MaterialButton console =
        UiKit.action(launch, R.string.ui_terminal_short, R.drawable.ic_console, true);
    console.setOnClickListener(
        v -> startActivity(new Intent(this, StandaloneTerminalActivity.class)));
    sessionButtons.add(console);
    MaterialButton desktop =
        UiKit.action(launch, R.string.ui_desktop_short, R.drawable.ic_display, false);
    desktop.setOnClickListener(v -> openDesktop());
    sessionButtons.add(desktop);
    connectionToggle = new MaterialSwitch(this);
    connectionToggle.setText(R.string.enabled);
    connectionToggle.setChecked(getSharedPreferences("config", 0).getBoolean("enabled", true));
    sessionBody.addView(connectionToggle);
    sessionBody.addView(text(getString(R.string.enabled_detail), 13));
    LinearLayout control = UiKit.actions(sessionBody);
    prepare = UiKit.action(control, R.string.ui_prepare_short, 0, false);
    prepare.setOnClickListener(v -> startRuntime());
    UiKit.action(control, R.string.ui_stop_short, R.drawable.ic_stop, false)
        .setOnClickListener(v -> confirmStop());
    UiKit.section(home, R.string.ui_android_name);
    androidGuestCard(home);
    UiKit.section(home, R.string.ui_quick_access);
    LinearLayout shortcuts = UiKit.group(home);
    UiKit.row(
        shortcuts,
        R.drawable.ic_backup,
        R.string.backup_title,
        R.string.ui_backup_summary,
        () -> PageMotion.open(this, BackupActivity.class));
    UiKit.row(
        shortcuts,
        R.drawable.ic_storage,
        R.string.asset_title,
        R.string.ui_assets_summary,
        () -> PageMotion.open(this, StorageManagementActivity.class));

    UiKit.heading(pages[1], R.string.ui_vm_tab, R.string.ui_vm_description);
    UiKit.section(pages[1], R.string.ui_debian_name);
    LinearLayout debian = UiKit.group(pages[1]);
    UiKit.row(
        debian,
        R.drawable.ic_folder,
        R.string.debian_setup,
        R.string.ui_setup_summary,
        () -> PageMotion.open(this, DebianSetupActivity.class));
    UiKit.row(
        debian,
        R.drawable.ic_display,
        R.string.ui_graphics_title,
        R.string.ui_graphics_summary,
        () -> showDetail(Detail.GRAPHICS));
    UiKit.row(
        debian,
        R.drawable.ic_vm,
        R.string.resources_title,
        R.string.ui_resources_summary,
        () -> showDetail(Detail.RESOURCES));
    UiKit.row(
        debian,
        R.drawable.ic_storage,
        R.string.storage_title,
        R.string.ui_disk_summary,
        () -> showDetail(Detail.DISK));
    UiKit.row(
        debian,
        R.drawable.ic_kernel,
        R.string.kernel_title,
        R.string.ui_kernel_summary,
        () -> showDetail(Detail.KERNEL));
    UiKit.section(pages[1], R.string.ui_android_name);
    androidGuestCard(pages[1]);

    UiKit.heading(pages[2], R.string.ui_devices_tab, R.string.ui_devices_description);
    UiKit.section(pages[2], R.string.ui_debian_sharing);
    LinearLayout devices = UiKit.group(pages[2]);
    UiKit.row(
        devices,
        R.drawable.ic_network,
        R.string.ui_network_short,
        R.string.ui_network_summary,
        () -> showDetail(Detail.NETWORK));
    UiKit.row(
        devices,
        R.drawable.ic_audio,
        R.string.ui_audio_short,
        R.string.ui_audio_summary,
        () -> showDetail(Detail.AUDIO));
    UiKit.row(
        devices,
        R.drawable.ic_clipboard,
        R.string.ui_clipboard_short,
        R.string.ui_clipboard_summary,
        () -> showDetail(Detail.CLIPBOARD));

    UiKit.heading(pages[3], R.string.tab_settings, R.string.ui_settings_description);
    UiKit.section(pages[3], R.string.ui_app_data);
    LinearLayout data = UiKit.group(pages[3]);
    UiKit.row(
        data,
        R.drawable.ic_backup,
        R.string.backup_title,
        R.string.ui_backup_summary,
        () -> PageMotion.open(this, BackupActivity.class));
    UiKit.row(
        data,
        R.drawable.ic_storage,
        R.string.asset_title,
        R.string.ui_assets_summary,
        () -> PageMotion.open(this, StorageManagementActivity.class));
    UiKit.section(pages[3], R.string.ui_optional);
    LinearLayout optional = UiKit.group(pages[3]);
    UiKit.row(
        optional,
        R.drawable.ic_link,
        R.string.ui_terminal_integration,
        R.string.ui_terminal_summary,
        () -> showDetail(Detail.TERMINAL));
    LinearLayout about = UiKit.group(pages[3]);
    UiKit.row(
        about,
        R.drawable.ic_help,
        R.string.ui_about,
        R.string.ui_about_summary,
        () -> showDetail(Detail.ABOUT));
    UiKit.row(about, R.drawable.ic_help, R.string.licenses, 0, this::licenses);

    graphicsCard(detailPage(Detail.GRAPHICS));
    resourcesCard(detailPage(Detail.RESOURCES));
    storageCard(detailPage(Detail.DISK));
    kernelCard(detailPage(Detail.KERNEL));
    networkCard(detailPage(Detail.NETWORK));
    LinearLayout audio = detailPage(Detail.AUDIO), clipboard = detailPage(Detail.CLIPBOARD);
    integrationCards(audio, clipboard);
    audioDevicesCard(audio);
    torchCard(audio);
    LinearLayout terminal = detailPage(Detail.TERMINAL);
    framework = new StatusCard(terminal, R.string.framework_title);
    LinearLayout terminalActions = UiKit.group(terminal);
    UiKit.row(
        terminalActions,
        R.drawable.ic_link,
        R.string.enable_terminal,
        R.string.ui_terminal_enable_summary,
        () -> setupTerminal(false));
    UiKit.row(
        terminalActions,
        R.drawable.ic_folder,
        R.string.install_linux,
        R.string.ui_stock_installer_summary,
        () -> setupTerminal(true));
    open = UiKit.action(terminal, R.string.open_terminal, R.drawable.ic_console, true);
    open.setOnClickListener(v -> openStockTerminal());
    LinearLayout help = detailPage(Detail.ABOUT);
    info(help, R.string.host17_title, R.string.host17_detail);
    info(help, R.string.setup_title, R.string.setup);
    info(help, R.string.limits_title, R.string.limits);
    info(help, R.string.network_title, R.string.network_detail);
    info(help, R.string.tools_title, R.string.tools_detail);
    info(help, R.string.gpu_title, R.string.gpu_detail);
    info(help, R.string.safety_title, R.string.safety);
    info(help, R.string.protection_title, R.string.host_protection);
    connectionToggle.setOnCheckedChangeListener(
        (view, on) -> {
          getSharedPreferences("config", 0).edit().putBoolean("enabled", on).apply();
          app().publish();
          open.setEnabled(on);
          sessionButtons.forEach(button -> button.setEnabled(on));
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
    open.setEnabled(connectionToggle.isChecked());
    sessionButtons.forEach(button -> button.setEnabled(connectionToggle.isChecked()));
    navigation.setSelectedItemId(100 + (saved == null ? 0 : saved.getInt("selectedTab", 0)));
    if (saved != null) {
      String key = saved.getString("selectedDetail");
      if (key != null)
        try {
          showDetail(Detail.valueOf(key));
        } catch (IllegalArgumentException ignored) {
        }
    }
    startRuntime();
  }

  private LinearLayout detailPage(Detail detail) {
    LinearLayout panel = column();
    panel.setBackgroundColor(UiKit.color(this, com.google.android.material.R.attr.colorSurface));
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(detail.title);
    toolbar.setNavigationIcon(R.drawable.ic_back);
    toolbar.setNavigationContentDescription(R.string.asset_back);
    toolbar.setNavigationOnClickListener(v -> closeDetail());
    panel.addView(toolbar);
    ScrollView scroll = new ScrollView(this);
    LinearLayout body = column();
    body.setPadding(dp(20), dp(8), dp(20), dp(28));
    scroll.addView(body);
    panel.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    detailHost.addView(panel, new FrameLayout.LayoutParams(-1, -1));
    detailViews.put(detail, panel);
    panel.setVisibility(View.GONE);
    return body;
  }

  private void showDetail(Detail detail) {
    if (selectedTab != detail.tab) navigation.setSelectedItemId(100 + detail.tab);
    View outgoing = selectedDetail == null ? pageHost.getChildAt(selectedTab) : detailHost;
    PageMotion.change(
        rootView,
        outgoing,
        detailHost,
        true,
        true,
        () -> {
          selectedDetail = detail;
          for (int index = 0; index < 4; index++)
            pageHost.getChildAt(index).setVisibility(View.GONE);
          for (var entry : detailViews.entrySet())
            entry.getValue().setVisibility(entry.getKey() == detail ? View.VISIBLE : View.GONE);
          detailHost.setVisibility(View.VISIBLE);
          navigation.setVisibility(View.GONE);
          detailBack.setEnabled(true);
          ViewCompat.requestApplyInsets(rootView);
        });
  }

  private void closeDetail() {
    View incoming = pageHost.getChildAt(selectedTab);
    PageMotion.change(
        rootView,
        selectedDetail == null ? incoming : detailHost,
        incoming,
        true,
        false,
        this::showTabViews);
  }

  private void selectTab(int next) {
    View outgoing = selectedDetail == null ? pageHost.getChildAt(selectedTab) : detailHost;
    PageMotion.change(
        rootView,
        outgoing,
        pageHost.getChildAt(next),
        false,
        true,
        () -> {
          selectedTab = next;
          showTabViews();
        });
  }

  private void showTabViews() {
    selectedDetail = null;
    detailHost.setVisibility(View.GONE);
    navigation.setVisibility(View.VISIBLE);
    detailBack.setEnabled(false);
    for (int index = 0; index < 4; index++)
      pageHost.getChildAt(index).setVisibility(index == selectedTab ? View.VISIBLE : View.GONE);
    ViewCompat.requestApplyInsets(rootView);
  }

  private void confirmStop() {
    new MaterialAlertDialogBuilder(this)
        .setTitle(R.string.stop_vm)
        .setMessage(R.string.stop_detail)
        .setNegativeButton(android.R.string.cancel, null)
        .setPositiveButton(
            android.R.string.ok,
            (dialog, which) -> {
              runtime.show(-1, R.string.checking);
              worker.submit(
                  () -> {
                    boolean ok = RuntimeSetup.stop(this);
                    runOnUiThread(
                        () -> {
                          if (!isDestroyed())
                            runtime.show(
                                ok ? 1 : 0, ok ? R.string.runtime_stopped : R.string.stop_failed);
                        });
                  });
            })
        .show();
  }

  @Override
  protected void onResume() {
    super.onResume();
    if (connectionToggle != null)
      connectionToggle.setChecked(getSharedPreferences("config", 0).getBoolean("enabled", true));
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
    androidx.transition.TransitionManager.endTransitions(rootView);
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
                    RuntimeSetup.message(result));
                refreshFramework();
                if ("ready".equals(result)) {
                  loadGraphics();
                  loadResources();
                  loadStorage();
                  loadNetwork();
                  applyClipboard();
                } else {
                  int message = RuntimeSetup.message(result);
                  graphicsStatus.setText(message);
                  resourceStatus.setText(message);
                  storageStatus.setText(message);
                  networkStatus.setText(message);
                }
              });
        });
  }

  private void setupTerminal(boolean installer) {
    runtime.show(-1, R.string.setup_running);
    worker.submit(
        () -> {
          TerminalSetup.Result result = TerminalSetup.run(this, installer);
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                if (result == TerminalSetup.Result.ALREADY_INSTALLED) {
                  runtime.show(2, R.string.stock_linux_installed_title);
                  new MaterialAlertDialogBuilder(this)
                      .setTitle(R.string.stock_linux_installed_title)
                      .setMessage(R.string.stock_linux_installed_detail)
                      .setPositiveButton(
                          R.string.open_terminal, (dialog, which) -> openStockTerminal())
                      .setNeutralButton(
                          R.string.stock_linux_manage,
                          (dialog, which) -> PageMotion.open(this, DebianSetupActivity.class))
                      .setNegativeButton(android.R.string.cancel, null)
                      .show();
                } else {
                  boolean ok = result != TerminalSetup.Result.FAILED;
                  runtime.show(ok ? 2 : 0, ok ? R.string.setup_done : R.string.setup_failed);
                  if (result == TerminalSetup.Result.ENABLED) startRuntime();
                }
              });
        });
  }

  private void openStockTerminal() {
    try {
      startActivity(
          new Intent()
              .setComponent(
                  new ComponentName(RuntimeSetup.HOST, RuntimeSetup.HOST + ".MainActivity")));
    } catch (Exception error) {
      runtime.show(0, R.string.terminal_missing);
    }
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
    UiKit.textAction(body, R.string.gpu_light_preset)
        .setOnClickListener(
            v ->
                new MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.gpu_light_preset)
                    .setMessage(R.string.gpu_light_detail)
                    .setNegativeButton(android.R.string.cancel, null)
                    .setPositiveButton(android.R.string.ok, (d, w) -> lightGpuPreset())
                    .show());
    LinearLayout graphicsActions = UiKit.actions(body);
    graphicsSave = UiKit.action(graphicsActions, R.string.ui_save_short, 0, true);
    graphicsSave.setOnClickListener(v -> saveGraphics(false));
    graphicsRestart = UiKit.action(graphicsActions, R.string.ui_save_restart_short, 0, false);
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
    LinearLayout networkActions = UiKit.actions(body);
    UiKit.action(networkActions, R.string.tun_refresh, 0, false)
        .setOnClickListener(v -> loadNetwork());
    networkSave = UiKit.action(networkActions, R.string.tun_apply, 0, true);
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
    LinearLayout kernelActions = UiKit.actions(body);
    UiKit.action(kernelActions, R.string.kernel_import, 0, false)
        .setOnClickListener(v -> kernelImporter.launch(new String[] {"*/*"}));
    MaterialButton apply = UiKit.action(kernelActions, R.string.ui_apply_short, 0, true);
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
  private MaterialButton storageCancel;

  private static String capacity(long bytes) {
    return String.format(java.util.Locale.getDefault(), "%.2f GiB", bytes / 1073741824.0);
  }

  private void storageCard(LinearLayout page) {
    MaterialCardView card = card(page);
    LinearLayout body = column();
    body.setPadding(dp(18), dp(14), dp(18), dp(16));
    card.addView(body);
    body.addView(text(getString(R.string.storage_title), 20));
    LinearLayout header = UiKit.actions(body);
    storageStatus = text(getString(R.string.checking), 14);
    storageStatus.setTextIsSelectable(true);
    header.addView(storageStatus, new LinearLayout.LayoutParams(0, -2, 1));
    MaterialButton refresh = UiKit.textAction(header, R.string.storage_refresh);
    storageButtons.add(refresh);
    refresh.setOnClickListener(view -> loadStorage());
    int[] labels = {
      R.string.storage_resize,
      R.string.storage_compact,
      R.string.storage_restore,
      R.string.storage_discard
    };
    String[] commands = {"resize", "compact", "restore", "discard"};
    int[] selected = {0};
    TextInputLayout operation = new TextInputLayout(this);
    operation.setHint(R.string.ui_operation);
    operation.setBoxBackgroundMode(TextInputLayout.BOX_BACKGROUND_OUTLINE);
    operation.setEndIconMode(TextInputLayout.END_ICON_DROPDOWN_MENU);
    MaterialAutoCompleteTextView menu = new MaterialAutoCompleteTextView(operation.getContext());
    menu.setInputType(0);
    menu.setSimpleItems(
        java.util.Arrays.stream(labels).mapToObj(this::getString).toArray(String[]::new));
    menu.setText(getString(labels[0]), false);
    operation.addView(menu);
    body.addView(operation);
    TextInputLayout field = new TextInputLayout(this);
    field.setHint(getString(R.string.storage_capacity));
    field.setBoxBackgroundMode(TextInputLayout.BOX_BACKGROUND_OUTLINE);
    storageSize = new com.google.android.material.textfield.TextInputEditText(field.getContext());
    storageSize.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
    field.addView(storageSize);
    body.addView(field);
    MaterialButton execute = UiKit.action(body, labels[0], R.drawable.ic_storage, true);
    storageButtons.add(execute);
    menu.setOnItemClickListener(
        (parent, view, index, id) -> {
          selected[0] = index;
          field.setVisibility(index == 0 ? View.VISIBLE : View.GONE);
          field.setError(null);
          execute.setText(labels[index]);
        });
    execute.setOnClickListener(
        view -> {
          int target = 0;
          String action = commands[selected[0]];
          if (action.equals("resize"))
            try {
              target = Integer.parseInt(String.valueOf(storageSize.getText()));
              if (target < 1 || target > 128) throw new NumberFormatException();
            } catch (NumberFormatException invalid) {
              field.setError(getString(R.string.storage_invalid));
              return;
            }
          field.setError(null);
          int capacity = target;
          new MaterialAlertDialogBuilder(this)
              .setTitle(labels[selected[0]])
              .setMessage(
                  action.equals("discard")
                      ? R.string.storage_discard_warning
                      : R.string.storage_warning)
              .setNegativeButton(android.R.string.cancel, null)
              .setPositiveButton(
                  android.R.string.ok, (dialog, which) -> runStorage(action, capacity))
              .show();
        });
    storageCancel = UiKit.textAction(body, R.string.storage_cancel);
    storageCancel.setVisibility(View.GONE);
    storageCancel.setOnClickListener(
        view -> {
          storageCancel.setEnabled(false);
          java.util.concurrent.CompletableFuture.runAsync(() -> RuntimeSetup.cancelStorage(this));
        });
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
    storageCancel.setVisibility(View.VISIBLE);
    storageCancel.setEnabled(true);
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
                storageCancel.setVisibility(View.GONE);
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
    body.setPadding(dp(18), dp(14), dp(18), dp(16));
    card.addView(body);
    body.addView(text(getString(R.string.android_guest_title), 20));
    body.addView(text(getString(R.string.android17_app_summary), 14));
    button(R.string.android_guest_open, body).setOnClickListener(view -> confirmAndroidGuest());
  }

  private void confirmAndroidGuest() {
    new MaterialAlertDialogBuilder(this)
        .setTitle(R.string.android_guest_title)
        .setMessage(R.string.android17_app_conditions)
        .setNegativeButton(android.R.string.cancel, null)
        .setPositiveButton(
            R.string.android_guest_open,
            (dialog, which) ->
                startActivity(
                    new Intent(this, AndroidGuestActivity.class)
                        .putExtra(AndroidGuestActivity.EXTRA_MEMORY_MIB, 1024)
                        .putExtra(AndroidGuestActivity.EXTRA_INTERNET, false)
                        .putExtra(AndroidGuestActivity.EXTRA_GPU_SURFACE, true)
                        .putExtra(AndroidGuestActivity.EXTRA_POLICY_ACKNOWLEDGED, true)))
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
    LinearLayout resourceActions = UiKit.actions(body);
    UiKit.action(resourceActions, R.string.ui_save_short, 0, true)
        .setOnClickListener(v -> saveResources(false));
    UiKit.action(resourceActions, R.string.ui_save_restart_short, 0, false)
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

  private void integrationCards(LinearLayout page, LinearLayout clipboardPage) {
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
    MaterialCardView clipboardCard = card(clipboardPage);
    body = column();
    body.setPadding(dp(18), dp(12), dp(18), dp(16));
    clipboardCard.addView(body);
    body.addView(text(getString(R.string.ui_clipboard_short), 20));
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
    LinearLayout transferActions = UiKit.actions(body);
    UiKit.action(transferActions, R.string.clip_to_linux, 0, false)
        .setOnClickListener(v -> ClipboardBridge.transfer(this, true, clipboardStatus::setText));
    UiKit.action(transferActions, R.string.clip_to_android, 0, false)
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
    TextView inventory = text(getString(R.string.checking), 13);
    body.addView(inventory);
    MaterialAutoCompleteTextView direction =
        selection(body, R.string.microphone_direction, R.array.microphone_directions);
    int selectedDirection = getSharedPreferences("config", 0).getInt("microphone_direction", 0);
    direction.setText(
        getResources()
            .getStringArray(R.array.microphone_directions)[
            selectedDirection >= 0 && selectedDirection <= 2 ? selectedDirection : 0],
        false);
    direction.setOnItemClickListener(
        (parent, view, index, id) -> {
          getSharedPreferences("config", 0).edit().putInt("microphone_direction", index).apply();
          MicrophoneService.reconfigure(this);
        });
    body.addView(text(getString(R.string.microphone_array_detail), 13));
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
    refreshAudioDevices =
        () -> {
          refresh.run();
          worker.submit(
              () -> {
                var snapshot = MicrophoneInventory.read(this);
                runOnUiThread(
                    () -> {
                      if (!isDestroyed())
                        inventory.setText(MicrophoneInventory.summary(this, snapshot));
                    });
              });
        };
    refreshAudioDevices.run();
    UiKit.textAction(body, R.string.audio_refresh_devices)
        .setOnClickListener(v -> refreshDevices());
    body.addView(text(getString(R.string.usb_direct_unavailable), 13));
  }

  private void torchCard(LinearLayout page) {
    LinearLayout body = column();
    body.setPadding(dp(18), dp(14), dp(18), dp(16));
    card(page).addView(body);
    body.addView(text(getString(R.string.vm_torch_title), 20));
    body.addView(text(getString(R.string.vm_torch_detail), 14));
    MaterialSwitch allow = new MaterialSwitch(this);
    allow.setText(R.string.vm_torch_allow);
    var preferences = getSharedPreferences("config", 0);
    allow.setChecked(preferences.getBoolean("vm_torch_enabled", false));
    body.addView(allow);
    MaterialAutoCompleteTextView camera =
        selection(body, R.string.vm_torch_camera, R.array.audio_device_placeholder);
    try {
      var choices = VmTorchBridge.available(this);
      camera.setSimpleItems(
          choices.stream()
              .map(c -> getString(R.string.vm_torch_choice, c.id(), c.maximum()))
              .toArray(String[]::new));
      String selected = preferences.getString("vm_torch_camera", "");
      for (var choice : choices)
        if (choice.id().equals(selected))
          camera.setText(getString(R.string.vm_torch_choice, choice.id(), choice.maximum()), false);
      camera.setOnItemClickListener(
          (parent, view, index, id) ->
              preferences.edit().putString("vm_torch_camera", choices.get(index).id()).apply());
      if (choices.isEmpty()) {
        allow.setEnabled(false);
        camera.setEnabled(false);
      }
    } catch (Exception unavailable) {
      allow.setEnabled(false);
      camera.setEnabled(false);
      body.addView(text(getString(R.string.vm_torch_unavailable), 14));
    }
    allow.setOnCheckedChangeListener(
        (button, checked) -> preferences.edit().putBoolean("vm_torch_enabled", checked).apply());
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
    if (selectedDetail != null) state.putString("selectedDetail", selectedDetail.name());
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
    int appearance =
        size >= 20
            ? com.google.android.material.R.style.TextAppearance_Material3_TitleLarge
            : size >= 16
                ? com.google.android.material.R.style.TextAppearance_Material3_TitleMedium
                : size <= 13
                    ? com.google.android.material.R.style.TextAppearance_Material3_BodySmall
                    : com.google.android.material.R.style.TextAppearance_Material3_BodyMedium;
    TextView view = UiKit.text(this, value, appearance);
    view.setPadding(0, dp(6), 0, dp(6));
    if (size < 16)
      view.setTextColor(
          UiKit.color(this, com.google.android.material.R.attr.colorOnSurfaceVariant));
    return view;
  }

  private MaterialCardView card(LinearLayout parent) {
    return UiKit.card(parent);
  }

  private MaterialButton button(int label, LinearLayout parent) {
    return UiKit.action(parent, label, 0, false);
  }

  private void info(LinearLayout parent, int title, int detail) {
    LinearLayout group = UiKit.group(parent);
    UiKit.row(
        group,
        R.drawable.ic_help,
        title,
        0,
        () -> {
          TextView copy = text(getString(detail), 14);
          copy.setPadding(dp(24), dp(8), dp(24), dp(8));
          ScrollView box = new ScrollView(this);
          box.addView(copy);
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
