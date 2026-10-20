package net.ryuya_dev.protectedlinux;

import android.app.*;
import android.content.*;
import android.os.*;
import io.github.libxposed.api.*;
import java.lang.reflect.*;

public final class HookEntry extends XposedModule {

  public void onModuleLoaded(XposedModuleInterface.ModuleLoadedParam p) {
    ModernHook.attach(this);
    try {
      RuntimePreferences.remote = getRemotePreferences("config");
    } catch (Throwable e) {
      android.util.Log.w("ProtectedLinux", "REMOTE_CONFIG_UNAVAILABLE");
    }
  }

  public void onPackageReady(XposedModuleInterface.PackageReadyParam p) {
    if (!p.isFirstPackage() || !RuntimeSetup.HOST.equals(p.getPackageName())) return;
    String apk = getModuleApplicationInfo().sourceDir;
    ModernHook.findAndHookMethod(
        Application.class,
        "attach",
        Context.class,
        new ModernHook.Callback() {
          protected void afterHookedMethod(ModernHook.Param call) {
            Context c = (Context) call.args[0];
            if (!RuntimeSetup.HOST.equals(Application.getProcessName())) return;
            java.util.List<ModernHook.Handle> installed = new java.util.ArrayList<>();
            try {
              if (!HostPlatform.compatible(c))
                throw new IllegalStateException("Unrecognized Android 17 ROM/Terminal");
              Class<?> main =
                  Class.forName(RuntimeSetup.HOST + ".MainActivity", false, p.getClassLoader());
              Class<?> fragment =
                  Class.forName(
                      RuntimeSetup.HOST + ".TerminalTabFragment", false, p.getClassLoader());
              TerminalContract.Methods contract =
                  TerminalContract.inspect(
                      main,
                      fragment,
                      android.view.View.class,
                      android.webkit.WebView.class,
                      Bundle.class,
                      Activity.class);
              Method start = contract.start(),
                  connect = contract.connect(),
                  destroy = contract.destroy(),
                  tabDestroy = contract.tabDestroy(),
                  viewCreated = contract.viewCreated(),
                  getActivity = contract.getActivity();
              for (Method caller : contract.callers())
                if (!ModernHook.deoptimize(caller))
                  throw new IllegalStateException("Caller unavailable");
              for (Method m :
                  new Method[] {
                    start, connect, destroy, tabDestroy, viewCreated, contract.viewDestroyed()
                  })
                if (!ModernHook.deoptimize(m))
                  throw new IllegalStateException("Deoptimization unavailable");
              installed.add(
                  ModernHook.hookMethod(
                      start,
                      new ModernHook.Callback() {
                        protected void beforeHookedMethod(ModernHook.Param x) {
                          boolean on = enabled(c);
                          android.util.Log.i("ProtectedLinux", "START_GATE enabled=" + on);
                          if (on) {
                            x.setResult(null);
                            Controller.prepare((Activity) x.thisObject, c, apk);
                          }
                        }
                      }));
              installed.add(
                  ModernHook.hookMethod(
                      connect,
                      new ModernHook.Callback() {
                        protected void beforeHookedMethod(ModernHook.Param x) {
                          if (enabled(c)) {
                            x.setResult(null);
                            Controller.bind((Activity) x.thisObject, x.args[0], c, apk);
                          }
                        }
                      }));
              installed.add(
                  ModernHook.hookMethod(
                      destroy,
                      new ModernHook.Callback() {
                        protected void beforeHookedMethod(ModernHook.Param x) {
                          Controller.close((Activity) x.thisObject);
                        }
                      }));
              installed.add(
                  ModernHook.hookMethod(
                      viewCreated,
                      new ModernHook.Callback() {
                        protected void afterHookedMethod(ModernHook.Param x) throws Throwable {
                          if (!x.hasThrowable() && enabled(c)) {
                            Activity activity = (Activity) getActivity.invoke(x.thisObject);
                            if (activity != null) Controller.bind(activity, x.thisObject, c, apk);
                          }
                        }
                      }));
              installed.add(
                  ModernHook.hookMethod(
                      tabDestroy,
                      new ModernHook.Callback() {
                        protected void beforeHookedMethod(ModernHook.Param x) {
                          Controller.closeFragment(x.thisObject);
                        }
                      }));
              installed.add(
                  ModernHook.hookMethod(
                      contract.viewDestroyed(),
                      new ModernHook.Callback() {
                        protected void beforeHookedMethod(ModernHook.Param x) {
                          // This inherited method belongs to AndroidX Fragment. Restrict
                          // cleanup to the confirmed Terminal fragment, not every fragment.
                          if (fragment.isInstance(x.thisObject))
                            Controller.closeFragment(x.thisObject);
                        }
                      }));
              status(c, "ready");
              android.util.Log.i("ProtectedLinux", "HOOK_READY api=102");
            } catch (Throwable e) {
              for (ModernHook.Handle handle : installed)
                try {
                  handle.unhook();
                } catch (Throwable ignored) {
                }
              status(c, "unsupported");
              android.util.Log.e("ProtectedLinux", "HOOK_STOP " + e.getClass().getSimpleName());
            }
          }
        });
  }

  static boolean enabled(Context c) {
    // An unavailable configuration must not fall into Samsung's unsupported non-protected launch.
    // The bridge instead displays a bounded setup error in the existing Terminal view.
    try {
      if (RuntimePreferences.remote != null && RuntimePreferences.remote.contains("enabled"))
        return RuntimePreferences.remote.getBoolean("enabled", false);
    } catch (Exception e) {
      android.util.Log.w("ProtectedLinux", "REMOTE_CONFIG_READ_FAILED");
    }
    android.util.Log.w("ProtectedLinux", "CONFIG_PENDING");
    return true;
  }

  static void status(Context c, String state) {
    android.util.Log.i("ProtectedLinux", "STATE " + state);
  }
}
