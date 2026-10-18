package net.ryuya_dev.protectedlinux;
import android.app.*;
import android.content.*;
import android.os.*;
import io.github.libxposed.api.*;
import java.lang.reflect.*;

public final class HookEntry extends XposedModule {

 public void onModuleLoaded(XposedModuleInterface.ModuleLoadedParam p){ModernHook.attach(this);try{RuntimePreferences.remote=getRemotePreferences("config");}catch(Throwable e){android.util.Log.w("ProtectedLinux","REMOTE_CONFIG_UNAVAILABLE");}}
 public void onPackageReady(XposedModuleInterface.PackageReadyParam p){
  if(!p.isFirstPackage()||!RuntimeSetup.HOST.equals(p.getPackageName()))return;
  String apk=getModuleApplicationInfo().sourceDir;
  ModernHook.findAndHookMethod(Application.class,"attach",Context.class,new ModernHook.Callback(){
   protected void afterHookedMethod(ModernHook.Param call){
    Context c=(Context)call.args[0];
    if(!RuntimeSetup.HOST.equals(Application.getProcessName()))return;
    java.util.List<ModernHook.Handle> installed=new java.util.ArrayList<>();
    try{
     if(Build.VERSION.SDK_INT!=36||c.getPackageManager().getPackageInfo(RuntimeSetup.HOST,0).getLongVersionCode()!=36)throw new IllegalStateException("Untested Terminal/OS version");
     Class<?> main=Class.forName(RuntimeSetup.HOST+".MainActivity",false,p.getClassLoader());
     Class<?> fragment=Class.forName(RuntimeSetup.HOST+".TerminalTabFragment",false,p.getClassLoader());
     Method start=main.getDeclaredMethod("startVm"),connect=main.getDeclaredMethod("connectToTerminalService",fragment),destroy=main.getDeclaredMethod("onDestroy");
     // Verify the whole UI contract before changing the VM launch path.
     for(String field:new String[]{"terminalView","bootProgressView","ttydStatus","ttydTimeoutRunnable"})fragment.getDeclaredField(field);
     main.getDeclaredField("tabAddButton");main.getDeclaredField("displayMenu");
     Method tabDestroy=fragment.getDeclaredMethod("onDestroy");ModernHook.deoptimize(tabDestroy);
     // APK's onCreate and synthetic access bridge call startVm; onViewCreated calls connect.
     for(Method caller:main.getDeclaredMethods())if(caller.getName().equals("onCreate")||caller.getName().contains("access$startVm")||caller.getName().equals("onAccessibilityStateChanged"))if(!ModernHook.deoptimize(caller))throw new IllegalStateException("Caller unavailable");
     for(Method caller:fragment.getDeclaredMethods())if(caller.getName().equals("onViewCreated"))if(!ModernHook.deoptimize(caller))throw new IllegalStateException("Fragment caller unavailable");
     for(Method m:new Method[]{start,connect,destroy})if(!ModernHook.deoptimize(m))throw new IllegalStateException("Deoptimization unavailable");
     installed.add(ModernHook.hookMethod(start,new ModernHook.Callback(){protected void beforeHookedMethod(ModernHook.Param x){boolean on=enabled(c);android.util.Log.i("ProtectedLinux","START_GATE enabled="+on);if(on){x.setResult(null);Controller.prepare((Activity)x.thisObject,c,apk);}}}));
     installed.add(ModernHook.hookMethod(connect,new ModernHook.Callback(){protected void beforeHookedMethod(ModernHook.Param x){if(enabled(c)){x.setResult(null);Controller.bind((Activity)x.thisObject,x.args[0],c,apk);}}}));
     installed.add(ModernHook.hookMethod(destroy,new ModernHook.Callback(){protected void beforeHookedMethod(ModernHook.Param x){Controller.close((Activity)x.thisObject);}}));
     Method viewCreated=fragment.getDeclaredMethod("onViewCreated",android.view.View.class,Bundle.class);
     Method getActivity=fragment.getMethod("getActivity");
     installed.add(ModernHook.hookMethod(viewCreated,new ModernHook.Callback(){protected void afterHookedMethod(ModernHook.Param x)throws Throwable{if(!x.hasThrowable()&&enabled(c)){Activity activity=(Activity)getActivity.invoke(x.thisObject);if(activity!=null)Controller.bind(activity,x.thisObject,c,apk);}}}));
     installed.add(ModernHook.hookMethod(tabDestroy,new ModernHook.Callback(){protected void beforeHookedMethod(ModernHook.Param x){Controller.closeFragment(x.thisObject);}}));
     status(c,"ready");android.util.Log.i("ProtectedLinux","HOOK_READY api=102");
    }catch(Throwable e){for(ModernHook.Handle handle:installed)try{handle.unhook();}catch(Throwable ignored){}status(c,"unsupported");android.util.Log.e("ProtectedLinux","HOOK_STOP "+e.getClass().getSimpleName());}
   }
  });
 }
 static boolean enabled(Context c){
  // An unavailable configuration must not fall into Samsung's unsupported non-protected launch.
  // The bridge instead displays a bounded setup error in the existing Terminal view.
  try { if(RuntimePreferences.remote!=null&&RuntimePreferences.remote.contains("enabled"))return RuntimePreferences.remote.getBoolean("enabled",false); }
  catch(Exception e){android.util.Log.w("ProtectedLinux","REMOTE_CONFIG_READ_FAILED");}
  android.util.Log.w("ProtectedLinux","CONFIG_PENDING");return true;
 }
 static void status(Context c,String state){android.util.Log.i("ProtectedLinux","STATE "+state);}
}
