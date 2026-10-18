package net.ryuya_dev.protectedlinux;

import android.content.Context;
import android.media.*;
import java.io.IOException;
import java.util.*;

/** Per-stream routing only. Never changes Android's global audio mode or pairing. */
final class AudioDevices {
  record Choice(String key, String label) {}

  static String key(AudioDeviceInfo d) {
    return d.getType() + "|" + d.getAddress() + "|" + d.getProductName();
  }

  static boolean external(AudioDeviceInfo d) {
    return switch (d.getType()) {
      case AudioDeviceInfo.TYPE_WIRED_HEADSET,
          AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
          AudioDeviceInfo.TYPE_USB_DEVICE,
          AudioDeviceInfo.TYPE_USB_ACCESSORY,
          AudioDeviceInfo.TYPE_USB_HEADSET,
          AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
          AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
          AudioDeviceInfo.TYPE_BLE_HEADSET,
          AudioDeviceInfo.TYPE_BLE_SPEAKER,
          AudioDeviceInfo.TYPE_HEARING_AID ->
          true;
      default -> false;
    };
  }

  static AudioDeviceInfo[] connected(Context c, boolean input) {
    try {
      return c.getSystemService(AudioManager.class)
          .getDevices(input ? AudioManager.GET_DEVICES_INPUTS : AudioManager.GET_DEVICES_OUTPUTS);
    } catch (RuntimeException unavailable) {
      return new AudioDeviceInfo[0];
    }
  }

  static List<Choice> choices(Context c, boolean input) {
    List<Choice> out = new ArrayList<>();
    out.add(new Choice("auto", c.getString(R.string.audio_auto_external)));
    out.add(
        new Choice(
            "builtin",
            c.getString(input ? R.string.audio_builtin_mic : R.string.audio_builtin_speaker)));
    for (AudioDeviceInfo d : connected(c, input))
      if (external(d)) out.add(new Choice(key(d), d.getProductName() + " · " + d.getId()));
    return out;
  }

  static AudioDeviceInfo preferred(Context c, boolean input) throws IOException {
    var p = c.getSharedPreferences("config", 0);
    boolean linux = p.getBoolean("peripheral_audio_linux", false);
    String selected =
        linux
            ? p.getString(input ? "audio_input" : "audio_output", input ? "builtin" : "auto")
            : "builtin";
    int builtin = input ? AudioDeviceInfo.TYPE_BUILTIN_MIC : AudioDeviceInfo.TYPE_BUILTIN_SPEAKER;
    AudioDeviceInfo[] devices = connected(c, input);
    List<AudioRoutePolicy.Device> candidates =
        Arrays.stream(devices)
            .map(
                d ->
                    new AudioRoutePolicy.Device(
                        d.getId(), key(d), d.getType() == builtin, external(d)))
            .toList();
    try {
      int id = AudioRoutePolicy.select(candidates, selected);
      return Arrays.stream(devices).filter(d -> d.getId() == id).findFirst().orElseThrow();
    } catch (AudioRoutePolicy.Missing missing) {
      throw new DeviceUnavailableException(c.getString(R.string.audio_device_missing));
    } catch (AudioRoutePolicy.Ambiguous ambiguous) {
      throw new DeviceUnavailableException(c.getString(R.string.audio_device_ambiguous));
    }
  }

  static final class DeviceUnavailableException extends IOException {
    DeviceUnavailableException(String text) {
      super(text);
    }
  }
}
