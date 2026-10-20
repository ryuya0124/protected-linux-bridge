package net.ryuya_dev.protectedlinux;

import android.content.res.ColorStateList;
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
import com.google.android.material.card.MaterialCardView;
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
  private TextView selectionCount;
  private Set<String> restoredSelection;
  private final List<MaterialButton> actions = new ArrayList<>();
  private final Map<MaterialCardView, Set<String>> groups = new LinkedHashMap<>();
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
    if (saved != null && saved.containsKey("selectedRoots"))
      restoredSelection = Set.copyOf(saved.getStringArrayList("selectedRoots"));
    LinearLayout root = column();
    PageMotion.configureActivity(this, root);
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.ui_app_data);
    toolbar.setNavigationIcon(R.drawable.ic_back);
    toolbar.setNavigationContentDescription(R.string.asset_back);
    toolbar.setNavigationOnClickListener(view -> finishAfterTransition());
    root.addView(toolbar);
    toolbar.getMenu().add(0, 1, 0, R.string.backup_recover);
    toolbar.getMenu().add(0, 2, 1, R.string.ui_about);
    toolbar.setOnMenuItemClickListener(
        item -> {
          if (item.getItemId() == 1) model.run("recover", null, Set.of());
          else
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.backup_title)
                .setMessage(
                    getString(R.string.backup_detail) + "\n\n" + getString(R.string.backup_privacy))
                .setPositiveButton(android.R.string.ok, null)
                .show();
          return true;
        });
    ScrollView scroll = new ScrollView(this);
    LinearLayout body = column();
    body.setPadding(dp(20), 0, dp(20), dp(16));
    scroll.addView(body);
    root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    UiKit.heading(body, R.string.backup_title, R.string.ui_backup_intro);
    LinearLayout state = UiKit.group(body);
    state.setPadding(dp(18), dp(12), dp(18), dp(12));
    status = text(getString(R.string.ui_maintenance_idle));
    state.addView(status);
    MaterialButton stop = UiKit.textAction(state, R.string.ui_stop_short);
    actions.add(stop);
    stop.setOnClickListener(
        view ->
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.stop_vm)
                .setMessage(R.string.stop_detail)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                    android.R.string.ok, (dialog, which) -> model.run("stop", null, Set.of()))
                .show());
    progress = new LinearProgressIndicator(this);
    progress.setIndeterminate(true);
    progress.setVisibility(View.GONE);
    state.addView(progress);
    UiKit.section(body, R.string.ui_backup_select);
    GridLayout grid = new GridLayout(this);
    grid.setColumnCount(
        getResources().getConfiguration().screenWidthDp < 360
                || getResources().getConfiguration().fontScale > 1.3
            ? 1
            : 2);
    body.addView(grid, new LinearLayout.LayoutParams(-1, -2));
    addGroup(
        grid,
        R.string.ui_backup_debian_short,
        R.string.ui_backup_debian_detail,
        R.drawable.ic_console,
        Set.of("vm", "vm-mainline", "debian-images"),
        true);
    addGroup(
        grid,
        R.string.ui_backup_android_short,
        R.string.ui_backup_android_detail,
        R.drawable.ic_vm,
        Set.of("android-guests"),
        false);
    addGroup(
        grid,
        R.string.ui_backup_kernel_short,
        R.string.ui_backup_kernel_detail,
        R.drawable.ic_kernel,
        Set.of("kernels"),
        true);
    addGroup(
        grid,
        R.string.ui_backup_settings_short,
        R.string.ui_backup_settings_detail,
        R.drawable.ic_settings,
        Set.of("settings"),
        true);
    LinearLayout selectionRow = UiKit.actions(body);
    selectionCount = text("");
    selectionRow.addView(selectionCount, new LinearLayout.LayoutParams(0, -2, 1));
    MaterialButton all = UiKit.textAction(selectionRow, R.string.backup_all);
    actions.add(all);
    all.setOnClickListener(view -> groups.keySet().forEach(card -> card.setChecked(true)));
    updateSelection();
    body.addView(text(getString(R.string.ui_debian_backup_scope)));
    LinearLayout dock = column();
    dock.setPadding(dp(20), dp(8), dp(20), dp(12));
    dock.setBackgroundColor(
        UiKit.color(this, com.google.android.material.R.attr.colorSurfaceContainer));
    root.addView(dock);
    LinearLayout buttons = UiKit.actions(dock);
    MaterialButton export =
        UiKit.action(buttons, R.string.ui_backup_create, R.drawable.ic_backup, true);
    actions.add(export);
    export.setOnClickListener(
        view -> {
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
    MaterialButton restore =
        UiKit.action(buttons, R.string.ui_restore_short, R.drawable.ic_restore, false);
    actions.add(restore);
    restore.setOnClickListener(
        view -> open.launch(new String[] {"application/zip", "application/octet-stream"}));
    MaterialButton cancel = UiKit.textAction(dock, android.R.string.cancel);
    cancel.setVisibility(View.GONE);
    cancel.setOnClickListener(view -> model.cancel());
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
    model.state.observe(
        this,
        value -> {
          actions.forEach(button -> button.setEnabled(!value.busy()));
          groups
              .keySet()
              .forEach(
                  card -> {
                    card.setEnabled(!value.busy());
                    card.setAlpha(value.busy() ? 0.55f : 1);
                  });
          toolbar.getMenu().findItem(1).setEnabled(!value.busy());
          progress.setVisibility(value.busy() ? View.VISIBLE : View.GONE);
          cancel.setVisibility(value.busy() ? View.VISIBLE : View.GONE);
          if (value.busy())
            status.setText(
                getString(
                    R.string.backup_progress, Formatter.formatShortFileSize(this, value.bytes())));
          else if (value.preview() != null) {
            model.consumePreview();
            preview(value.preview());
          } else if (!value.result().isEmpty()) status.setText(message(value.result()));
        });
  }

  @Override
  protected void onSaveInstanceState(Bundle state) {
    super.onSaveInstanceState(state);
    state.putStringArrayList("selectedRoots", new ArrayList<>(selected()));
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

  private void addGroup(
      GridLayout grid, int title, int detail, int icon, Set<String> roots, boolean checked) {
    MaterialCardView card = UiKit.surface(this);
    card.setCheckable(true);
    card.setStrokeWidth(dp(1));
    card.setStrokeColor(UiKit.color(this, com.google.android.material.R.attr.colorOutlineVariant));
    card.setCheckedIconResource(R.drawable.ic_check);
    card.setCheckedIconSize(dp(22));
    card.setCheckedIconMargin(dp(14));
    card.setCheckedIconTint(
        ColorStateList.valueOf(UiKit.color(this, androidx.appcompat.R.attr.colorPrimary)));
    LinearLayout content = column();
    content.setPadding(dp(16), dp(18), dp(16), dp(18));
    content.setMinimumHeight(dp(154));
    card.addView(content);
    ImageView image = new ImageView(this);
    image.setImageResource(icon);
    image.setImageTintList(
        ColorStateList.valueOf(UiKit.color(this, androidx.appcompat.R.attr.colorPrimary)));
    image.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
    content.addView(image, new LinearLayout.LayoutParams(dp(28), dp(28)));
    TextView label =
        UiKit.text(
            this,
            getText(title),
            com.google.android.material.R.style.TextAppearance_Material3_TitleMedium);
    label.setPadding(0, dp(14), 0, dp(4));
    content.addView(label);
    TextView description =
        UiKit.text(
            this,
            getText(detail),
            com.google.android.material.R.style.TextAppearance_Material3_BodySmall);
    description.setTextColor(
        UiKit.color(this, com.google.android.material.R.attr.colorOnSurfaceVariant));
    content.addView(description);
    card.setContentDescription(getString(title) + ". " + getString(detail));
    content.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
    card.setOnClickListener(view -> card.toggle());
    card.setOnCheckedChangeListener(
        (view, on) -> {
          card.setStrokeWidth(dp(on ? 2 : 1));
          card.setStrokeColor(
              UiKit.color(
                  this,
                  on
                      ? androidx.appcompat.R.attr.colorPrimary
                      : com.google.android.material.R.attr.colorOutlineVariant));
          card.setCardBackgroundColor(
              UiKit.color(
                  this,
                  on
                      ? com.google.android.material.R.attr.colorPrimaryContainer
                      : com.google.android.material.R.attr.colorSurfaceContainerLow));
          int foreground =
              UiKit.color(
                  this,
                  on
                      ? com.google.android.material.R.attr.colorOnPrimaryContainer
                      : com.google.android.material.R.attr.colorOnSurface);
          label.setTextColor(foreground);
          description.setTextColor(
              UiKit.color(
                  this,
                  on
                      ? com.google.android.material.R.attr.colorOnPrimaryContainer
                      : com.google.android.material.R.attr.colorOnSurfaceVariant));
          image.setImageTintList(ColorStateList.valueOf(foreground));
          updateSelection();
        });
    GridLayout.LayoutParams layout = new GridLayout.LayoutParams();
    layout.width = 0;
    layout.height = -2;
    layout.columnSpec = GridLayout.spec(GridLayout.UNDEFINED, GridLayout.FILL, 1f);
    layout.rowSpec = GridLayout.spec(GridLayout.UNDEFINED, GridLayout.FILL, 1f);
    layout.setMargins(dp(4), dp(4), dp(4), dp(4));
    grid.addView(card, layout);
    groups.put(card, roots);
    card.setChecked(restoredSelection == null ? checked : restoredSelection.containsAll(roots));
  }

  private void updateSelection() {
    if (selectionCount != null)
      selectionCount.setText(
          getString(
              R.string.ui_selection_count,
              groups.keySet().stream().filter(MaterialCardView::isChecked).count()));
  }

  private TextView text(String value) {
    TextView view =
        UiKit.text(
            this, value, com.google.android.material.R.style.TextAppearance_Material3_BodyMedium);
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
