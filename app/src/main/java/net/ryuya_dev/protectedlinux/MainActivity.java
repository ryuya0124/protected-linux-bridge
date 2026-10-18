package net.ryuya_dev.protectedlinux;

import android.content.*;
import android.content.res.ColorStateList;
import android.os.*;
import android.view.*;
import android.widget.*;
import androidx.appcompat.app.AppCompatActivity;
import androidx.activity.EdgeToEdge;
import androidx.core.view.*;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.card.MaterialCardView;
import com.google.android.material.color.DynamicColors;
import com.google.android.material.color.MaterialColors;
import com.google.android.material.materialswitch.MaterialSwitch;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.CircularProgressIndicator;
import com.google.android.material.appbar.MaterialToolbar;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;

public final class MainActivity extends AppCompatActivity {
  private final ExecutorService worker=Executors.newSingleThreadExecutor();
  private StatusCard runtime,framework;
  private MaterialButton prepare,open;
  private boolean preparing;
  private BridgeApplication app(){return (BridgeApplication)getApplication();}
  @Override public void onCreate(Bundle saved) {
    DynamicColors.applyToActivityIfAvailable(this);super.onCreate(saved);EdgeToEdge.enable(this);
    ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);
    LinearLayout page=column();page.setPadding(dp(20),dp(12),dp(20),dp(28));scroll.addView(page);setContentView(scroll);
    ViewCompat.setOnApplyWindowInsetsListener(scroll,(v,insets)->{var bars=insets.getInsets(WindowInsetsCompat.Type.systemBars()|WindowInsetsCompat.Type.displayCutout());v.setPadding(bars.left,bars.top,bars.right,bars.bottom);return insets;});
    MaterialToolbar toolbar=new MaterialToolbar(this);toolbar.setTitle(R.string.app_name);toolbar.setLogo(R.drawable.ic_terminal);page.addView(toolbar);
    page.addView(text(getString(R.string.intro),20));page.addView(text(getString(R.string.experimental),13));
    runtime=new StatusCard(page,R.string.root_title);framework=new StatusCard(page,R.string.framework_title);
    MaterialCardView toggleCard=card(page);LinearLayout toggleBody=column();toggleBody.setPadding(dp(16),dp(8),dp(16),dp(8));toggleCard.addView(toggleBody);
    MaterialSwitch toggle=new MaterialSwitch(this);toggle.setText(R.string.enabled);toggle.setChecked(getSharedPreferences("config",0).getBoolean("enabled",true));toggleBody.addView(toggle);toggleBody.addView(text(getString(R.string.enabled_detail),13));
    button(R.string.enable_terminal,page).setOnClickListener(v->setupTerminal(false));
    button(R.string.install_linux,page).setOnClickListener(v->setupTerminal(true));
    open=button(R.string.open_terminal,page);
    open.setOnClickListener(v->{try{startActivity(new Intent().setComponent(new ComponentName(RuntimeSetup.HOST,RuntimeSetup.HOST+".MainActivity")));}catch(Exception error){runtime.show(0,R.string.terminal_missing);}});
    prepare=button(R.string.prepare,page);prepare.setOnClickListener(v->startRuntime());
    button(R.string.stop_vm,page).setOnClickListener(v->new MaterialAlertDialogBuilder(this).setTitle(R.string.stop_vm).setMessage(R.string.stop_detail).setNegativeButton(android.R.string.cancel,null).setPositiveButton(android.R.string.ok,(d,w)->{runtime.show(-1,R.string.checking);worker.submit(()->{boolean ok=RuntimeSetup.stop(this);runOnUiThread(()->{if(!isDestroyed())runtime.show(ok?1:0,ok?R.string.runtime_stopped:R.string.stop_failed);});});}).show());
    toggle.setOnCheckedChangeListener((v,on)->{getSharedPreferences("config",0).edit().putBoolean("enabled",on).apply();app().publish();open.setEnabled(on);if(on)startRuntime();else runtime.show(1,R.string.runtime_disabled);});
    open.setEnabled(toggle.isChecked());
    info(page,R.string.setup_title,R.string.setup);info(page,R.string.limits_title,R.string.limits);info(page,R.string.network_title,R.string.network_detail);info(page,R.string.safety_title,R.string.safety);
    button(R.string.licenses,page).setOnClickListener(v->licenses());
    startRuntime();
  }
  @Override protected void onResume(){super.onResume();app().observer=this::refreshFramework;refreshFramework();}
  @Override protected void onPause(){app().observer=null;super.onPause();}
  @Override protected void onDestroy(){worker.shutdownNow();super.onDestroy();}
  private void refreshFramework(){
    if(framework==null)return;int state=app().frameworkState();
    framework.show(state==3?2:state==2?1:0,switch(state){case 1->R.string.scope_missing;case 2->R.string.scope_ready;case 3->R.string.module_loaded;default->R.string.framework_unavailable;});
  }
  private void startRuntime(){
    if(preparing)return;preparing=true;prepare.setEnabled(false);runtime.show(-1,R.string.requesting_root);
    worker.submit(()->{String result=RuntimeSetup.start(this);runOnUiThread(()->{if(isDestroyed())return;preparing=false;prepare.setEnabled(true);runtime.show("ready".equals(result)?2:"disabled".equals(result)?1:0,"ready".equals(result)?R.string.runtime_ready:"disabled".equals(result)?R.string.runtime_disabled:"unsupported_version".equals(result)?R.string.unsupported_version:R.string.runtime_unavailable);refreshFramework();});});
  }
  private void setupTerminal(boolean installer){
    runtime.show(-1,R.string.setup_running);worker.submit(()->{boolean ok=TerminalSetup.run(installer);runOnUiThread(()->{if(!isDestroyed()){runtime.show(ok?2:0,ok?R.string.setup_done:R.string.setup_failed);if(ok&&!installer)startRuntime();}});});
  }
  private int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
  private LinearLayout column(){LinearLayout v=new LinearLayout(this);v.setOrientation(LinearLayout.VERTICAL);return v;}
  private TextView text(String value,int size){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setPadding(0,dp(8),0,dp(8));return t;}
  private MaterialCardView card(LinearLayout parent){MaterialCardView c=new MaterialCardView(this);c.setRadius(dp(24));LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.setMargins(0,dp(6),0,dp(6));parent.addView(c,p);return c;}
  private MaterialButton button(int label,LinearLayout parent){MaterialButton b=new MaterialButton(this);b.setText(label);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.setMargins(0,dp(4),0,dp(4));parent.addView(b,p);return b;}
  private void info(LinearLayout parent,int title,int detail){MaterialCardView c=card(parent);LinearLayout body=column();body.setPadding(dp(18),dp(8),dp(18),dp(8));c.addView(body);body.addView(text(getString(title),18));body.addView(text(getString(detail),14));}
  private void licenses(){try(var own=getAssets().open("APP_LICENSE.txt");var other=getAssets().open("THIRD_PARTY_NOTICES.txt")){
    TextView t=text(new String(own.readAllBytes(),StandardCharsets.UTF_8)+"\n\n"+new String(other.readAllBytes(),StandardCharsets.UTF_8),12);t.setTextIsSelectable(true);t.setPadding(dp(20),dp(8),dp(20),dp(8));ScrollView box=new ScrollView(this);box.addView(t);new MaterialAlertDialogBuilder(this).setTitle(R.string.licenses).setView(box).setPositiveButton(android.R.string.ok,null).show();
  }catch(IOException error){android.util.Log.e("ProtectedLinux","LICENSE_READ_FAILED");}}
  private final class StatusCard {
    private final TextView detail;private final ImageView icon;private final CircularProgressIndicator progress;
    StatusCard(LinearLayout parent,int label){MaterialCardView c=card(parent);LinearLayout row=new LinearLayout(MainActivity.this);row.setGravity(Gravity.CENTER_VERTICAL);row.setPadding(dp(16),dp(10),dp(16),dp(10));c.addView(row);FrameLayout leading=new FrameLayout(MainActivity.this);row.addView(leading,new LinearLayout.LayoutParams(dp(32),dp(32)));icon=new ImageView(MainActivity.this);leading.addView(icon,new FrameLayout.LayoutParams(-1,-1));progress=new CircularProgressIndicator(MainActivity.this);progress.setIndicatorSize(dp(28));progress.setTrackThickness(dp(3));progress.setIndeterminate(true);leading.addView(progress,new FrameLayout.LayoutParams(-1,-1));LinearLayout copy=column();LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);p.setMarginStart(dp(16));row.addView(copy,p);copy.addView(text(getString(label),18));detail=text(getString(R.string.checking),14);copy.addView(detail);show(-1,R.string.checking);}
    void show(int state,int message){detail.setText(message);progress.setVisibility(state<0?View.VISIBLE:View.GONE);icon.setVisibility(state<0?View.GONE:View.VISIBLE);icon.setImageResource(state==2?R.drawable.ic_check:state==1?R.drawable.ic_wait:R.drawable.ic_warning);icon.setImageTintList(ColorStateList.valueOf(MaterialColors.getColor(MainActivity.this,state==0?androidx.appcompat.R.attr.colorError:androidx.appcompat.R.attr.colorPrimary,0xff6750a4)));icon.setContentDescription(getString(state==2?R.string.state_ready:state==1?R.string.state_waiting:R.string.state_attention));}
  }
}
