package net.ryuya_dev.protectedlinux;

import java.lang.reflect.*;
import java.util.*;

/** Whole reflective UI contract, verified before any launch hook is installed. */
final class TerminalContract {
  record Methods(
      Method start,
      Method connect,
      Method destroy,
      Method viewCreated,
      Method tabDestroy,
      Method viewDestroyed,
      Method getActivity,
      List<Method> callers) {}

  static Methods inspect(
      Class<?> main,
      Class<?> fragment,
      Class<?> view,
      Class<?> web,
      Class<?> bundle,
      Class<?> activity)
      throws ReflectiveOperationException {
    fieldType(main, "tabAddButton", view);
    fieldType(main, "displayMenu", view);
    Class<?> terminalView = fieldType(fragment, "terminalView", web);
    fieldType(fragment, "bootProgressView", view);
    fieldType(fragment, "ttydStatus", int.class);
    fieldType(fragment, "ttydTimeoutRunnable", Runnable.class);
    requireVoid(terminalView.getMethod("mapCtrlKey"));
    Method start = requireVoid(main.getDeclaredMethod("startVm"));
    Method connect = requireVoid(main.getDeclaredMethod("connectToTerminalService", fragment));
    Method destroy = requireVoid(main.getDeclaredMethod("onDestroy"));
    Method created = requireVoid(fragment.getDeclaredMethod("onViewCreated", view, bundle));
    Method tabDestroy = requireVoid(fragment.getDeclaredMethod("onDestroy"));
    Method viewDestroyed = requireVoid(fragment.getMethod("onDestroyView"));
    Method getActivity = fragment.getMethod("getActivity");
    if (!activity.isAssignableFrom(getActivity.getReturnType()))
      throw new NoSuchMethodException("Fragment activity type");
    List<Method> callers = new ArrayList<>();
    callers.add(requireVoid(main.getDeclaredMethod("onCreate", bundle)));
    callers.add(requireVoid(main.getDeclaredMethod("onAccessibilityStateChanged", boolean.class)));
    for (Method method : main.getDeclaredMethods())
      if (method.getName().contains("access$startVm")) callers.add(method);
    // In the inspected APK this is AndroidX Fragment's real dispatch method.
    callers.add(
        requireVoid(viewDestroyed.getDeclaringClass().getDeclaredMethod("performDestroyView")));
    return new Methods(
        start,
        connect,
        destroy,
        created,
        tabDestroy,
        viewDestroyed,
        getActivity,
        List.copyOf(callers));
  }

  private static Class<?> fieldType(Class<?> owner, String name, Class<?> expected)
      throws NoSuchFieldException {
    Field field = owner.getDeclaredField(name);
    if (Modifier.isStatic(field.getModifiers()) || !expected.isAssignableFrom(field.getType()))
      throw new NoSuchFieldException(name + " type");
    field.setAccessible(true);
    return field.getType();
  }

  private static Method requireVoid(Method method) throws NoSuchMethodException {
    if (method.getReturnType() != void.class || Modifier.isStatic(method.getModifiers()))
      throw new NoSuchMethodException(method.getName() + " signature");
    method.setAccessible(true);
    return method;
  }
}
