package net.ryuya_dev.protectedlinux;

import android.util.Log;
import io.github.libxposed.api.XposedInterface;
import io.github.libxposed.api.XposedModule;
import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

/** Small project-local facade over libxposed API 102's interceptor-chain model. */
final class ModernHook {
  private static volatile XposedModule module;

  private ModernHook() {}

  static void attach(XposedModule value) {
    if (value.getApiVersion() < XposedInterface.API_102)
      throw new IllegalStateException("Xposed API 102 is required");
    module = value;
  }

  static int apiVersion() {
    return requireModule().getApiVersion();
  }

  static String frameworkDescription() {
    XposedModule current = requireModule();
    return current.getFrameworkName() + " " + current.getFrameworkVersion();
  }

  static Class<?> findClass(String name, ClassLoader loader) throws ClassNotFoundException {
    return Class.forName(name, false, loader);
  }

  static Handle findAndHookMethod(Class<?> owner, String name, Object... signature) {
    if (signature.length == 0 || !(signature[signature.length - 1] instanceof Callback callback))
      throw new IllegalArgumentException("Last argument must be a callback");
    Class<?>[] parameters = new Class<?>[signature.length - 1];
    for (int i = 0; i < parameters.length; i++) parameters[i] = (Class<?>) signature[i];
    try {
      Method method = owner.getDeclaredMethod(name, parameters);
      method.setAccessible(true);
      return hookMethod(method, callback);
    } catch (NoSuchMethodException e) {
      throw new IllegalStateException(owner.getName() + "#" + name + " signature changed", e);
    }
  }

  static Handle hookMethod(Executable executable, Callback callback) {
    XposedInterface.HookHandle handle =
        requireModule()
            .hook(executable)
            .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
            .intercept(
                chain -> {
                  Param param = new Param(chain);
                  callback.beforeHookedMethod(param);
                  if (!param.returnEarly) {
                    try {
                      param.result = chain.proceed(param.args);
                    } catch (Throwable t) {
                      param.throwable = t;
                    }
                  }
                  callback.afterHookedMethod(param);
                  if (param.throwable != null) throw param.throwable;
                  return param.result;
                });
    return handle::unhook;
  }

  interface Around {
    Object run(XposedInterface.Chain chain) throws Throwable;
  }

  static Handle aroundMethod(Executable executable, Around around) {
    XposedInterface.HookHandle handle =
        requireModule()
            .hook(executable)
            .setExceptionMode(XposedInterface.ExceptionMode.PROTECTIVE)
            .intercept(around::run);
    return handle::unhook;
  }

  static List<Handle> hookAllConstructors(Class<?> owner, Callback callback) {
    List<Handle> result = new ArrayList<>();
    try {
      for (Constructor<?> constructor : owner.getDeclaredConstructors()) {
        constructor.setAccessible(true);
        result.add(hookMethod(constructor, callback));
      }
      return result;
    } catch (Throwable t) {
      for (Handle handle : result) handle.unhook();
      if (t instanceof RuntimeException runtime) throw runtime;
      if (t instanceof Error error) throw error;
      throw new IllegalStateException(t);
    }
  }

  static boolean deoptimize(Executable executable) {
    return requireModule().deoptimize(executable);
  }

  static void log(String message) {
    XposedModule current = module;
    if (current != null) current.log(Log.INFO, "ProtectedLinux", message);
  }

  interface Handle {
    void unhook();
  }

  abstract static class Callback {
    protected void beforeHookedMethod(Param param) throws Throwable {}

    protected void afterHookedMethod(Param param) throws Throwable {}
  }

  static final class Param {
    final Object thisObject;
    final Object[] args;
    private Object result;
    private Throwable throwable;
    private boolean returnEarly;
    private final java.util.Map<String, Object> extras = new java.util.HashMap<>();

    Param(XposedInterface.Chain chain) {
      thisObject = chain.getThisObject();
      args = chain.getArgs().toArray();
    }

    boolean hasThrowable() {
      return throwable != null;
    }

    Object getResult() {
      return result;
    }

    void setResult(Object value) {
      result = value;
      throwable = null;
      returnEarly = true;
    }

    void setObjectExtra(String key, Object value) {
      extras.put(key, value);
    }

    Object getObjectExtra(String key) {
      return extras.get(key);
    }
  }

  private static XposedModule requireModule() {
    XposedModule current = module;
    if (current == null) throw new IllegalStateException("Modern Xposed module is not attached");
    return current;
  }
}
