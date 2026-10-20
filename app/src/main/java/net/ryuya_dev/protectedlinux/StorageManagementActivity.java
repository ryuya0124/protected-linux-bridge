package net.ryuya_dev.protectedlinux;

import android.os.Bundle;
import android.text.format.Formatter;
import android.view.ViewGroup;
import android.widget.*;
import androidx.activity.EdgeToEdge;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.*;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import java.util.concurrent.*;
import org.json.*;

/** Asset accounting stays usable without Vector and never starts a VM. */
public final class StorageManagementActivity extends AppCompatActivity {
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private LinearLayout items;
  private TextView status;
  private MaterialButton refresh;

  @Override
  protected void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);
    super.onCreate(saved);
    EdgeToEdge.enable(this);
    LinearLayout root = column();
    PageMotion.configureActivity(this, root);
    MaterialToolbar toolbar = new MaterialToolbar(this);
    toolbar.setTitle(R.string.asset_title);
    toolbar.setNavigationIcon(R.drawable.ic_back);
    toolbar.setNavigationContentDescription(R.string.asset_back);
    toolbar.setNavigationOnClickListener(v -> finishAfterTransition());
    root.addView(toolbar);
    ScrollView scroll = new ScrollView(this);
    items = column();
    items.setPadding(dp(16), dp(8), dp(16), dp(24));
    scroll.addView(items);
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
    refresh();
  }

  private void refresh() {
    items.removeAllViews();
    status = text(getString(R.string.checking), 16);
    items.addView(status);
    refresh = UiKit.textAction(items, R.string.storage_refresh);
    refresh.setEnabled(false);
    refresh.setOnClickListener(v -> refresh());
    worker.submit(
        () -> {
          JSONObject result = null;
          try {
            result = AssetInventory.read(this);
          } catch (Exception error) {
            android.util.Log.w(
                "ProtectedLinux",
                "ASSET_INVENTORY_UNAVAILABLE " + error.getClass().getSimpleName());
          }
          JSONObject value = result;
          runOnUiThread(
              () -> {
                if (isDestroyed()) return;
                refresh.setEnabled(true);
                if (value == null) {
                  status.setText(R.string.asset_unavailable);
                  return;
                }
                try {
                  render(value);
                } catch (JSONException invalid) {
                  status.setText(R.string.asset_unavailable);
                }
              });
        });
  }

  private void render(JSONObject result) throws JSONException {
    long total = result.getLong("total"), available = result.getLong("available");
    status.setText(getString(R.string.asset_device_space, size(available), size(total)));
    LinearProgressIndicator usage = new LinearProgressIndicator(this);
    usage.setMax(10000);
    usage.setProgress(
        total <= 0
            ? 0
            : (int) Math.max(0, Math.min(10000, (1.0 - (double) available / total) * 10000)));
    items.addView(usage, new LinearLayout.LayoutParams(-1, dp(12)));
    items.addView(
        text(
            getString(
                result.getInt("busy") == 1 ? R.string.asset_running : R.string.asset_read_only),
            14));
    JSONArray entries = result.getJSONArray("items");
    java.util.Map<String, JSONObject> catalog = new java.util.HashMap<>();
    for (int index = 0; index < entries.length(); index++) {
      JSONObject item = entries.getJSONObject(index);
      if (catalog.put(item.getString("id"), item) != null)
        throw new JSONException("duplicate catalog");
    }
    assetGroup(
        catalog,
        R.string.ui_debian_name,
        new String[] {"debian-images", "vm", "vm-mainline", "kernels"},
        new int[] {
          R.string.backup_debian_image,
          R.string.asset_debian,
          R.string.asset_mainline,
          R.string.asset_kernels
        });
    assetGroup(
        catalog,
        R.string.ui_android_name,
        new String[] {"android-guests", "android-gpu4g-runtime", "android-gpu4g-surface-runtime"},
        new int[] {R.string.asset_android, R.string.asset_gpu, R.string.asset_surface});
    assetGroup(
        catalog,
        R.string.ui_app_data,
        new String[] {"runtime", "backup-recovery", "backup-jobs", "kernel-probes"},
        new int[] {
          R.string.asset_runtime,
          R.string.backup_previous,
          R.string.backup_staging,
          R.string.asset_diagnostics
        });
    if (!catalog.isEmpty()) throw new JSONException("unknown catalog");
    items.addView(text(getString(R.string.asset_explanation), 14));
  }

  private void assetGroup(
      java.util.Map<String, JSONObject> catalog, int title, String[] ids, int[] titles)
      throws JSONException {
    UiKit.section(items, title);
    for (int index = 0; index < ids.length; index++) {
      JSONObject entry = catalog.remove(ids[index]);
      if (entry == null) throw new JSONException("missing catalog");
      MaterialCardView card = UiKit.card(items);
      LinearLayout body = column();
      body.setPadding(dp(18), dp(16), dp(18), dp(16));
      card.addView(body);
      body.addView(text(getString(titles[index]), 19));
      body.addView(
          text(
              getString(
                  R.string.asset_sizes,
                  size(entry.getLong("allocated")),
                  size(entry.getLong("logical")),
                  entry.getLong("files")),
              15));
      if (entry.getBoolean("partial")) body.addView(text(getString(R.string.asset_partial), 14));
      else if (!entry.getBoolean("present"))
        body.addView(text(getString(R.string.asset_absent), 14));
    }
  }

  private String size(long bytes) {
    return Formatter.formatShortFileSize(this, Math.max(0, bytes));
  }

  private LinearLayout column() {
    LinearLayout layout = new LinearLayout(this);
    layout.setOrientation(LinearLayout.VERTICAL);
    return layout;
  }

  private TextView text(String value, int sp) {
    TextView view =
        UiKit.text(
            this,
            value,
            sp >= 18
                ? com.google.android.material.R.style.TextAppearance_Material3_TitleMedium
                : com.google.android.material.R.style.TextAppearance_Material3_BodyMedium);
    view.setPadding(0, dp(8), 0, dp(8));
    view.setTextIsSelectable(true);
    view.setLayoutParams(new ViewGroup.LayoutParams(-1, -2));
    return view;
  }

  private int dp(int value) {
    return Math.round(value * getResources().getDisplayMetrics().density);
  }

  @Override
  protected void onDestroy() {
    worker.shutdownNow();
    super.onDestroy();
  }
}
