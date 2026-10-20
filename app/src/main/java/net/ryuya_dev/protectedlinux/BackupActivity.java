package net.ryuya_dev.protectedlinux;

import android.os.Bundle;
import android.text.format.Formatter;
import android.view.View;
import android.widget.*;
import androidx.activity.EdgeToEdge;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.*;
import androidx.lifecycle.ViewModelProvider;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.checkbox.MaterialCheckBox;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import java.util.*;
import net.ryuya_dev.protectedlinux.backup.BackupArchive;

/**
 * Individual compressed bundles using Android's document picker. Root is needed only for data I/O.
 */
public final class BackupActivity extends AppCompatActivity {
  private BackupModel model;
  private TextView status;
  private LinearProgressIndicator progress;
  private final List<MaterialButton> actions = new ArrayList<>();
  private final Map<MaterialCheckBox, Set<String>> groups = new LinkedHashMap<>();
  private final ActivityResultLauncher<String> create =
      registerForActivityResult(
          new ActivityResultContracts.CreateDocument("application/zip"),
          uri -> {
            if (uri != null)
              model.run("export", uri, model.exporting.isEmpty() ? selected() : model.exporting);
          });
  private final ActivityResultLauncher<String[]> open =
      registerForActivityResult(
          new ActivityResultContracts.OpenDocument(),
          uri -> {
            if (uri != null) model.inspect(uri);
          });

  @Override
  protected void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    EdgeToEdge.enable(this);
    model = new ViewModelProvider(this).get(BackupModel.class);
    LinearLayout root = column();
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.backup_title);
    toolbar.setNavigationIcon(R.drawable.ic_back);
    toolbar.setNavigationContentDescription(R.string.asset_back);
    toolbar.setNavigationOnClickListener(v -> finish());
    root.addView(toolbar);
    ScrollView scroll = new ScrollView(this);
    LinearLayout body = column();
    body.setPadding(dp(20), dp(12), dp(20), dp(24));
    scroll.addView(body);
    root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    setContentView(root);
    ViewCompat.setOnApplyWindowInsetsListener(
        root,
        (view, insets) -> {
          var bars =
              insets.getInsets(
                  WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout());
          view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
          return insets;
        });
    body.addView(text(getString(R.string.backup_detail)));
    addGroup(body, R.string.backup_debian, Set.of("vm", "vm-mainline", "debian-images"), true);
    addGroup(body, R.string.backup_android, Set.of("android-guests"), false);
    addGroup(body, R.string.backup_kernels, Set.of("kernels"), true);
    addGroup(body, R.string.backup_settings, Set.of("settings"), true);
    button(body, R.string.backup_all)
        .setOnClickListener(v -> groups.keySet().forEach(box -> box.setChecked(true)));
    status = text(getString(R.string.backup_idle));
    body.addView(status);
    progress = new LinearProgressIndicator(this);
    progress.setIndeterminate(true);
    progress.setVisibility(View.GONE);
    body.addView(progress);
    button(body, R.string.backup_export)
        .setOnClickListener(
            v -> {
              model.exporting = selected();
              if (model.exporting.isEmpty()) {
                status.setText(R.string.backup_select);
                return;
              }
              new MaterialAlertDialogBuilder(this)
                  .setTitle(R.string.backup_export)
                  .setMessage(R.string.backup_privacy)
                  .setNegativeButton(android.R.string.cancel, null)
                  .setPositiveButton(
                      android.R.string.ok,
                      (dialog, which) ->
                          create.launch("ProtectedLinux-" + java.time.LocalDate.now() + ".plb.zip"))
                  .show();
            });
    button(body, R.string.backup_restore)
        .setOnClickListener(
            v -> open.launch(new String[] {"application/zip", "application/octet-stream"}));
    button(body, R.string.stop_vm)
        .setOnClickListener(
            v ->
                new MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.stop_vm)
                    .setMessage(R.string.stop_detail)
                    .setNegativeButton(android.R.string.cancel, null)
                    .setPositiveButton(
                        android.R.string.ok, (dialog, which) -> model.run("stop", null, Set.of()))
                    .show());
    button(body, R.string.backup_recover)
        .setOnClickListener(v -> model.run("recover", null, Set.of()));
    MaterialButton cancel = new MaterialButton(this);
    cancel.setText(android.R.string.cancel);
    body.addView(cancel);
    cancel.setOnClickListener(v -> model.cancel());
    model.state.observe(
        this,
        state -> {
          actions.forEach(button -> button.setEnabled(!state.busy()));
          groups.keySet().forEach(box -> box.setEnabled(!state.busy()));
          progress.setVisibility(state.busy() ? View.VISIBLE : View.GONE);
          cancel.setEnabled(state.busy());
          if (state.busy())
            status.setText(
                getString(
                    R.string.backup_progress, Formatter.formatShortFileSize(this, state.bytes())));
          else if (state.preview() != null) {
            model.consumePreview();
            preview(state.preview());
          } else if (!state.result().isEmpty()) status.setText(message(state.result()));
        });
  }

  private void preview(BackupArchive.Info info) {
    List<String> roots = info.roots();
    boolean[] checked = new boolean[roots.size()];
    Arrays.fill(checked, true);
    String[] labels = roots.stream().map(this::rootLabel).toArray(String[]::new);
    status.setText(
        getString(
            R.string.backup_preview,
            info.sdk(),
            info.build(),
            info.files(),
            Formatter.formatShortFileSize(this, info.bytes())));
    new MaterialAlertDialogBuilder(this)
        .setTitle(R.string.backup_restore)
        .setMultiChoiceItems(labels, checked, (dialog, which, value) -> checked[which] = value)
        .setNegativeButton(android.R.string.cancel, null)
        .setPositiveButton(
            R.string.backup_restore,
            (dialog, which) -> {
              Set<String> selected = new TreeSet<>();
              for (int i = 0; i < checked.length; i++) if (checked[i]) selected.add(roots.get(i));
              if (selected.isEmpty()) {
                status.setText(R.string.backup_select);
                return;
              }
              new MaterialAlertDialogBuilder(this)
                  .setTitle(R.string.backup_restore)
                  .setMessage(R.string.backup_restore_confirm)
                  .setNegativeButton(android.R.string.cancel, null)
                  .setPositiveButton(
                      android.R.string.ok,
                      (confirmation, position) -> model.run("restore", model.restoring, selected))
                  .show();
            })
        .show();
  }

  private String rootLabel(String root) {
    return getString(
        switch (root) {
          case "vm" -> R.string.asset_debian;
          case "vm-mainline" -> R.string.asset_mainline;
          case "debian-images" -> R.string.backup_debian_image;
          case "android-guests" -> R.string.backup_android;
          case "kernels" -> R.string.backup_kernels;
          default -> R.string.backup_settings;
        });
  }

  private int message(String result) {
    return switch (result) {
      case "EXPORT_DONE" -> R.string.backup_export_done;
      case "RESTORE_DONE" -> R.string.backup_restore_done;
      case "RECOVER_DONE" -> R.string.backup_recovered;
      case "STOP_DONE" -> R.string.runtime_stopped;
      case "ARCHIVE_BUSY" -> R.string.backup_busy;
      case "ARCHIVE_CANCELLED" -> R.string.backup_cancelled;
      case "ARCHIVE_EMPTY" -> R.string.backup_empty;
      case "ARCHIVE_SPACE" -> R.string.backup_space;
      case "RESTORE_DEPENDENCY" -> R.string.backup_dependency;
      default -> R.string.backup_failed;
    };
  }

  private Set<String> selected() {
    Set<String> result = new TreeSet<>();
    groups.forEach(
        (box, roots) -> {
          if (box.isChecked()) result.addAll(roots);
        });
    return Set.copyOf(result);
  }

  private void addGroup(LinearLayout body, int title, Set<String> roots, boolean checked) {
    MaterialCheckBox box = new MaterialCheckBox(this);
    box.setText(title);
    box.setChecked(checked);
    body.addView(box);
    groups.put(box, roots);
  }

  private MaterialButton button(LinearLayout body, int title) {
    MaterialButton button = new MaterialButton(this);
    button.setText(title);
    body.addView(button);
    actions.add(button);
    return button;
  }

  private TextView text(String value) {
    TextView view = new TextView(this);
    view.setText(value);
    view.setTextSize(15);
    view.setPadding(0, dp(8), 0, dp(8));
    return view;
  }

  private LinearLayout column() {
    LinearLayout view = new LinearLayout(this);
    view.setOrientation(LinearLayout.VERTICAL);
    return view;
  }

  private int dp(int value) {
    return Math.round(value * getResources().getDisplayMetrics().density);
  }
}
