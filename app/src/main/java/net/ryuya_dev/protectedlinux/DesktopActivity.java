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
 private AudioBridge audio;private boolean resumed,displayReady;
 void displayReady(){runOnUiThread(()->{displayReady=true;if(resumed)startAudio();});}
 private void startAudio(){if(audio!=null||!displayReady)return;audio=new AudioBridge(this,message->android.widget.Toast.makeText(this,message,android.widget.Toast.LENGTH_SHORT).show());var p=getSharedPreferences("config",0);boolean background=p.getBoolean("microphone_background",false);if(background)MicrophoneService.apply(this);audio.start(p.getBoolean("speaker_enabled",true),p.getBoolean("microphone_enabled",false)&&!background);}
 @Override protected void onResume(){super.onResume();resumed=true;DisplayController.settingsChanged(this);startAudio();}
 @Override protected void onPause(){resumed=false;if(audio!=null){audio.close();audio=null;}super.onPause();}
 @Override public void onCreate(Bundle state){DynamicColors.applyToActivityIfAvailable(this);super.onCreate(state);int orientation=getSharedPreferences("config",0).getInt("desktop_orientation",0);setRequestedOrientation(orientation==1?android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT:orientation==2?android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE:android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);EdgeToEdge.enable(this);WindowInsetsControllerCompat bars=WindowCompat.getInsetsController(getWindow(),getWindow().getDecorView());bars.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);bars.hide(WindowInsetsCompat.Type.systemBars());DisplayController.openStandalone(this,getApplicationInfo().sourceDir);}
 @Override public void onConfigurationChanged(android.content.res.Configuration config){super.onConfigurationChanged(config);DisplayController.settingsChanged(this);}
 @Override protected void onDestroy(){DisplayController.close(this);super.onDestroy();}
}
