package net.ryuya_dev.protectedlinux;

import android.os.Bundle;
import android.text.format.Formatter;
import android.view.View;
import android.widget.*;
import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.*;
import androidx.lifecycle.ViewModelProvider;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import java.util.*;

/** Debian-only provisioning, with one primary path and an optional existing-image import. */
public final class DebianSetupActivity extends AppCompatActivity {
  @Override
  protected void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    EdgeToEdge.enable(this);
    BackupModel model = new ViewModelProvider(this).get(BackupModel.class);
    LinearLayout root = UiKit.column(this);
    PageMotion.configureActivity(this, root);
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.ui_debian_name);
    toolbar.setNavigationIcon(R.drawable.ic_back);
    toolbar.setNavigationContentDescription(R.string.asset_back);
    toolbar.setNavigationOnClickListener(view -> finishAfterTransition());
    root.addView(toolbar);
    toolbar
        .getMenu()
        .add(R.string.ui_about)
        .setOnMenuItemClickListener(
            item -> {
              new MaterialAlertDialogBuilder(this)
                  .setTitle(R.string.debian_setup)
                  .setMessage(R.string.debian_setup_detail)
                  .setPositiveButton(android.R.string.ok, null)
                  .show();
              return true;
            });
    ScrollView scroll = new ScrollView(this);
    LinearLayout body = UiKit.column(this);
    int padding = UiKit.dp(this, 20);
    body.setPadding(padding, 0, padding, padding);
    scroll.addView(body);
    root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    UiKit.heading(body, R.string.debian_setup, R.string.ui_install_scope);
    LinearLayout image = UiKit.group(body);
    image.setPadding(padding, padding, padding, padding);
    image.addView(
        UiKit.text(
            this,
            getText(R.string.ui_debian_image_title),
            com.google.android.material.R.style.TextAppearance_Material3_HeadlineMedium));
    TextView subtitle =
        UiKit.text(
            this,
            getText(R.string.ui_debian_image_detail),
            com.google.android.material.R.style.TextAppearance_Material3_BodyMedium);
    subtitle.setPadding(0, UiKit.dp(this, 8), 0, padding);
    image.addView(subtitle);
    LinearLayout metrics = UiKit.actions(image);
    metric(metrics, R.string.ui_image_download_size, "580 MiB");
    metric(metrics, R.string.ui_image_free_space, getString(R.string.ui_image_space_value));
    MaterialButton download =
        UiKit.action(image, R.string.ui_download_short, R.drawable.ic_download, true);
    download.setOnClickListener(
        view ->
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.debian_download)
                .setMessage(R.string.debian_download_confirm)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                    android.R.string.ok,
                    (dialog, which) -> model.run("debian-download", null, Set.of()))
                .show());
    LinearLayout alternative = UiKit.group(body);
    View importer =
        UiKit.row(
            alternative,
            R.drawable.ic_folder,
            R.string.ui_import_short,
            R.string.ui_import_detail,
            () ->
                new MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.debian_import)
                    .setMessage(R.string.debian_import_confirm)
                    .setNegativeButton(android.R.string.cancel, null)
                    .setPositiveButton(
                        android.R.string.ok,
                        (dialog, which) -> model.run("debian-import", null, Set.of()))
                    .show());
    LinearLayout work = UiKit.group(body);
    work.setPadding(padding, UiKit.dp(this, 12), padding, UiKit.dp(this, 12));
    TextView status =
        UiKit.text(
            this,
            getText(R.string.ui_maintenance_idle),
            com.google.android.material.R.style.TextAppearance_Material3_BodyMedium);
    work.addView(status);
    MaterialButton stop = UiKit.textAction(work, R.string.ui_stop_short);
    stop.setOnClickListener(
        view ->
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.stop_vm)
                .setMessage(R.string.stop_detail)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                    android.R.string.ok, (dialog, which) -> model.run("stop", null, Set.of()))
                .show());
    LinearProgressIndicator progress = new LinearProgressIndicator(this);
    progress.setIndeterminate(true);
    progress.setVisibility(View.GONE);
    work.addView(progress);
    MaterialButton cancel = UiKit.textAction(work, android.R.string.cancel);
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
          download.setEnabled(!value.busy());
          importer.setEnabled(!value.busy());
          stop.setEnabled(!value.busy());
          progress.setVisibility(value.busy() ? View.VISIBLE : View.GONE);
          cancel.setVisibility(value.busy() ? View.VISIBLE : View.GONE);
          if (value.busy())
            status.setText(
                getString(
                    R.string.backup_progress, Formatter.formatShortFileSize(this, value.bytes())));
          else if (!value.result().isEmpty())
            status.setText(
                switch (value.result()) {
                  case "DEBIAN-DOWNLOAD_DONE", "DEBIAN-IMPORT_DONE" -> R.string.debian_installed;
                  case "STOP_DONE" -> R.string.runtime_stopped;
                  case "ARCHIVE_BUSY" -> R.string.backup_busy;
                  case "ARCHIVE_IMAGE_EXISTS" -> R.string.debian_exists;
                  case "ARCHIVE_IMAGE_MISSING" -> R.string.debian_missing;
                  case "ARCHIVE_SPACE" -> R.string.backup_space;
                  case "ARCHIVE_CANCELLED" -> R.string.backup_cancelled;
                  default -> R.string.debian_failed;
                });
        });
  }

  private void metric(LinearLayout parent, int label, String value) {
    LinearLayout column = UiKit.column(this);
    parent.addView(column, new LinearLayout.LayoutParams(0, -2, 1));
    column.addView(
        UiKit.text(
            this,
            getText(label),
            com.google.android.material.R.style.TextAppearance_Material3_LabelMedium));
    TextView amount =
        UiKit.text(
            this, value, com.google.android.material.R.style.TextAppearance_Material3_TitleLarge);
    amount.setPadding(0, UiKit.dp(this, 4), 0, UiKit.dp(this, 16));
    column.addView(amount);
  }
}
