package net.ryuya_dev.protectedlinux;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.MicrophoneInfo;
import java.io.IOException;
import java.util.List;

/** Metadata only. A logical input/one PCM channel need not mean one physical capsule. */
final class MicrophoneInventory {
  record Snapshot(int builtinPorts, int describedMicrophones, boolean known) {}

  static Snapshot read(Context context) {
    AudioManager manager = context.getSystemService(AudioManager.class);
    if (manager == null) return new Snapshot(0, 0, false);
    int ports = 0;
    for (AudioDeviceInfo device : manager.getDevices(AudioManager.GET_DEVICES_INPUTS))
      if (device.getType() == AudioDeviceInfo.TYPE_BUILTIN_MIC) ports++;
    try {
      List<MicrophoneInfo> microphones = manager.getMicrophones();
      return new Snapshot(ports, microphones.size(), true);
    } catch (IOException | RuntimeException unavailable) {
      return new Snapshot(ports, 0, false);
    }
  }

  static String summary(Context context, Snapshot snapshot) {
    return context.getString(
        snapshot.known() ? R.string.microphone_inventory : R.string.microphone_inventory_unknown,
        snapshot.builtinPorts(),
        snapshot.describedMicrophones());
  }

  private MicrophoneInventory() {}
}
