package net.ryuya_dev.protectedlinux;

import android.app.Application;
import android.util.Log;
import io.github.libxposed.service.XposedService;
import io.github.libxposed.service.XposedServiceHelper;

/** Config is shared through the framework, not through target app package visibility. */
public final class BridgeApplication extends Application {
  private volatile XposedService service;
  volatile Runnable observer;
  private void notifyState(){new android.os.Handler(getMainLooper()).post(()->{Runnable r=observer;if(r!=null)r.run();});}
  int frameworkState(){
    XposedService current=service;if(current==null)return 0;
    try {
      if(!current.getScope().contains(RuntimeSetup.HOST))return 1;
      if(current.getApiVersion()>=102)for(var target:current.getRunningTargets())if(target.getProcessName().equals(RuntimeSetup.HOST)&&target.getState()==io.github.libxposed.service.HookedTarget.State.UP_TO_DATE)return 3;
      return 2;
    }catch(RuntimeException e){return 0;}
  }
  @Override public void onCreate() {
    super.onCreate();
    XposedServiceHelper.registerListener(new XposedServiceHelper.OnServiceListener() {
      public void onServiceBind(XposedService value) { service=value; publish();notifyState(); }
      public void onServiceDied(XposedService value) { if(service==value)service=null;notifyState(); }
    });
  }
  void publish() {
    XposedService current=service;
    if(current==null)return;
    try {
      boolean ok=current.getRemotePreferences("config").edit().putBoolean("enabled",
        getSharedPreferences("config",0).getBoolean("enabled",true)).commit();
      Log.i("ProtectedLinux", "CONFIG_PUBLISHED="+ok);
    } catch(RuntimeException error) { Log.w("ProtectedLinux","CONFIG_PUBLISH_FAILED "+error.getClass().getSimpleName()); }
  }
}
