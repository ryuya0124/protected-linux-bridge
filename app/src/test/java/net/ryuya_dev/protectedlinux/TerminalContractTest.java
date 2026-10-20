package net.ryuya_dev.protectedlinux;

import static org.junit.Assert.*;

import org.junit.Test;

public class TerminalContractTest {
  static class View {}

  static class Bundle {}

  static class Activity {}

  public static class Web extends View {
    public void mapCtrlKey() {}
  }

  public static class FragmentBase {
    public Activity getActivity() {
      return null;
    }

    public void onDestroyView() {}

    void performDestroyView() {
      onDestroyView();
    }
  }

  public static class Fragment extends FragmentBase {
    Web terminalView;
    View bootProgressView;
    int ttydStatus;
    Runnable ttydTimeoutRunnable;

    public void onViewCreated(View view, Bundle state) {}

    public void onDestroy() {}
  }

  static class Main {
    View tabAddButton, displayMenu;

    void startVm() {}

    void connectToTerminalService(Fragment fragment) {}

    void onDestroy() {}

    void onCreate(Bundle state) {}

    void onAccessibilityStateChanged(boolean enabled) {}
  }

  static class WrongReturn extends Main {
    View tabAddButton, displayMenu;

    // A name match alone must not select this method.
    int connectToTerminalService(String fragment) {
      return 0;
    }
  }

  static class WrongFields {
    String tabAddButton;
    View displayMenu;
  }

  @Test
  public void resolvesInheritedViewTeardownAndExactSignatures() throws Exception {
    TerminalContract.Methods contract = inspect(Main.class);
    assertEquals(FragmentBase.class, contract.viewDestroyed().getDeclaringClass());
    assertEquals(Fragment.class, contract.connect().getParameterTypes()[0]);
    assertTrue(
        contract.callers().stream()
            .anyMatch(method -> method.getName().equals("performDestroyView")));
  }

  @Test
  public void rejectsChangedFieldTypeBeforeAnyHookCanBeInstalled() {
    assertThrows(NoSuchFieldException.class, () -> inspect(WrongFields.class));
  }

  @Test
  public void rejectsInheritedOrChangedLaunchMethods() {
    assertThrows(NoSuchMethodException.class, () -> inspect(WrongReturn.class));
  }

  private TerminalContract.Methods inspect(Class<?> main) throws ReflectiveOperationException {
    return TerminalContract.inspect(
        main, Fragment.class, View.class, Web.class, Bundle.class, Activity.class);
  }
}
