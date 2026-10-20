package net.ryuya_dev.protectedlinux.backup;

import java.io.*;
import java.util.*;

/** Only known local settings; restored capture/automatic integration stays disabled. */
public final class PreferenceSnapshot {
  private static final Set<String> FLAGS =
      Set.of(
          "enabled",
          "mainline_kernel",
          "gpu_display",
          "follow_rotation",
          "microphone_background",
          "microphone_enabled",
          "speaker_enabled",
          "peripheral_audio_linux",
          "clipboard_auto",
          "clipboard_to_android",
          "clipboard_to_linux");
  private static final Set<String> NUMBERS =
      Set.of("resolution_id", "display_fps", "desktop_orientation");

  private PreferenceSnapshot() {}

  public static byte[] encode(Map<String, ?> preferences) throws IOException {
    Properties properties = new Properties();
    properties.setProperty("schema", "1");
    for (String key : FLAGS)
      if (preferences.get(key) instanceof Boolean value) properties.setProperty(key, "b:" + value);
    for (String key : NUMBERS)
      if (preferences.get(key) instanceof Integer value) properties.setProperty(key, "i:" + value);
    ByteArrayOutputStream output = new ByteArrayOutputStream();
    properties.store(output, "Protected Linux Bridge settings");
    return output.toByteArray();
  }

  public static Map<String, Object> decode(byte[] data) throws IOException {
    if (data.length > 65536) throw new IOException("ARCHIVE_SETTINGS");
    Properties properties = new Properties();
    properties.load(new ByteArrayInputStream(data));
    if (!properties.getProperty("schema", "").equals("1"))
      throw new IOException("ARCHIVE_SETTINGS");
    Map<String, Object> result = new HashMap<>();
    for (String key : properties.stringPropertyNames()) {
      if (key.equals("schema")) continue;
      String value = properties.getProperty(key);
      if (FLAGS.contains(key) && Set.of("b:true", "b:false").contains(value))
        result.put(key, value.equals("b:true"));
      else if (NUMBERS.contains(key) && value.matches("i:-?[0-9]{1,4}")) {
        int number = Integer.parseInt(value.substring(2));
        if (key.equals("resolution_id") && number >= 0 && number <= 8
            || key.equals("display_fps") && Set.of(15, 24, 30, 60, 90, 120).contains(number)
            || key.equals("desktop_orientation") && number >= 0 && number <= 2)
          result.put(key, number);
        else throw new IOException("ARCHIVE_SETTINGS");
      } else throw new IOException("ARCHIVE_SETTINGS");
    }
    result.put("enabled", false);
    result.put("microphone_enabled", false);
    result.put("clipboard_auto", false);
    return Map.copyOf(result);
  }
}
