package net.ryuya_dev.protectedlinux;

import java.util.List;

/** Selection never falls back from a missing explicit device to another route. */
final class AudioRoutePolicy {
  record Device(int id, String key, boolean builtin, boolean external) {}

  static final class Missing extends Exception {}

  static final class Ambiguous extends Exception {}

  static int select(List<Device> devices, String key) throws Missing, Ambiguous {
    var matches =
        devices.stream()
            .filter(
                d ->
                    "builtin".equals(key)
                        ? d.builtin()
                        : "auto".equals(key) ? d.external() : d.key().equals(key))
            .toList();
    if (matches.isEmpty()) throw new Missing();
    if (!"builtin".equals(key) && matches.size() > 1) throw new Ambiguous();
    return matches.get(0).id();
  }
}
