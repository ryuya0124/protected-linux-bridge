package net.ryuya_dev.protectedlinux;
import android.os.Bundle;
import androidx.appcompat.app.AppCompatActivity;
import androidx.activity.EdgeToEdge;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;
import com.google.android.material.color.DynamicColors;
/** A separate GUI connection. Opening/closing never stops existing Terminal PTYs. */
public final class DesktopActivity extends AppCompatActivity {
 @Override public void onCreate(Bundle state){DynamicColors.applyToActivityIfAvailable(this);super.onCreate(state);EdgeToEdge.enable(this);WindowInsetsControllerCompat bars=WindowCompat.getInsetsController(getWindow(),getWindow().getDecorView());bars.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);bars.hide(WindowInsetsCompat.Type.systemBars());DisplayController.openStandalone(this,getApplicationInfo().sourceDir);}
 @Override protected void onDestroy(){DisplayController.close(this);super.onDestroy();}
}
