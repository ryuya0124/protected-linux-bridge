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

/** First install/import is in Bridge. The stock Terminal UI and Vector are optional. */
public final class DebianSetupActivity extends AppCompatActivity {
  @Override
  protected void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    EdgeToEdge.enable(this);
    BackupModel model = new ViewModelProvider(this).get(BackupModel.class);
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.debian_setup);
    toolbar.setNavigationIcon(R.drawable.ic_back);
    toolbar.setNavigationContentDescription(R.string.asset_back);
    toolbar.setNavigationOnClickListener(v -> finish());
    root.addView(toolbar);
    ScrollView scroll = new ScrollView(this);
    LinearLayout body = new LinearLayout(this);
    body.setOrientation(LinearLayout.VERTICAL);
    int padding = Math.round(20 * getResources().getDisplayMetrics().density);
    body.setPadding(padding, padding, padding, padding);
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
    TextView detail = new TextView(this);
    detail.setText(R.string.debian_setup_detail);
    body.addView(detail);
    TextView status = new TextView(this);
    status.setText(R.string.debian_setup_idle);
    status.setPadding(0, padding, 0, padding);
    body.addView(status);
    LinearProgressIndicator progress = new LinearProgressIndicator(this);
    progress.setIndeterminate(true);
    progress.setVisibility(View.GONE);
    body.addView(progress);
    List<MaterialButton> actions = new ArrayList<>();
    for (boolean download : new boolean[] {true, false}) {
      MaterialButton button = new MaterialButton(this);
      actions.add(button);
      body.addView(button);
      button.setText(download ? R.string.debian_download : R.string.debian_import);
      button.setOnClickListener(
          v ->
              new MaterialAlertDialogBuilder(this)
                  .setTitle(button.getText())
                  .setMessage(
                      download ? R.string.debian_download_confirm : R.string.debian_import_confirm)
                  .setNegativeButton(android.R.string.cancel, null)
                  .setPositiveButton(
                      android.R.string.ok,
                      (dialog, which) ->
                          model.run(download ? "debian-download" : "debian-import", null, Set.of()))
                  .show());
    }
    MaterialButton stop = new MaterialButton(this);
    stop.setText(R.string.stop_vm);
    body.addView(stop);
    actions.add(stop);
    stop.setOnClickListener(
        v ->
            new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.stop_vm)
                .setMessage(R.string.stop_detail)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                    android.R.string.ok, (dialog, which) -> model.run("stop", null, Set.of()))
                .show());
    MaterialButton cancel = new MaterialButton(this);
    cancel.setText(android.R.string.cancel);
    body.addView(cancel);
    cancel.setOnClickListener(v -> model.cancel());
    model.state.observe(
        this,
        value -> {
          actions.forEach(button -> button.setEnabled(!value.busy()));
          cancel.setEnabled(value.busy());
          progress.setVisibility(value.busy() ? View.VISIBLE : View.GONE);
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
}
