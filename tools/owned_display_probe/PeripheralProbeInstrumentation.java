package net.ryuya_dev.protectedlinux;

import android.Manifest;
import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.camera2.CameraManager;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Bundle;
import java.util.EnumSet;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import org.json.JSONArray;
import org.json.JSONObject;

/** No Activity, root, recording stream, camera open, permission request or host input. */
public final class PeripheralProbeInstrumentation extends Instrumentation {
  @Override
  public void onCreate(Bundle arguments) {
    super.onCreate(arguments);
    start();
  }

  @Override
  public void onStart() {
    Bundle result = new Bundle();
    try {
      Context context = getTargetContext();
      JSONObject inventory = new JSONObject();
      JSONArray sensors = new JSONArray();
      for (var kind : AndroidGuestSensorSource.available(context)) sensors.put(kind.name());
      inventory.put("sensor_kinds", sensors);
      AudioManager audio = context.getSystemService(AudioManager.class);
      JSONArray outputs = new JSONArray(), inputs = new JSONArray();
      for (AudioDeviceInfo device : audio.getDevices(AudioManager.GET_DEVICES_OUTPUTS))
        outputs.put(device.getType());
      for (AudioDeviceInfo device : audio.getDevices(AudioManager.GET_DEVICES_INPUTS))
        inputs.put(device.getType());
      inventory.put("audio_output_types", outputs);
      inventory.put("audio_input_types", inputs);
      inventory.put(
          "microphone_permission",
          context.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
              == PackageManager.PERMISSION_GRANTED);
      inventory.put(
          "camera_permission",
          context.checkSelfPermission(Manifest.permission.CAMERA)
              == PackageManager.PERMISSION_GRANTED);
      AtomicInteger deniedCameraFrames = new AtomicInteger();
      boolean cameraConsentDenied = false;
      try (AndroidGuestCameraSource cameraSource =
          new AndroidGuestCameraSource(
              context,
              () -> false,
              image -> deniedCameraFrames.incrementAndGet(),
              error -> deniedCameraFrames.incrementAndGet())) {
        try {
          cameraSource.start("0", new android.util.Size(640, 480), 30);
        } catch (SecurityException expected) {
          cameraConsentDenied = true;
        }
      }
      inventory.put(
          "camera_opt_in_gate_passed", cameraConsentDenied && deniedCameraFrames.get() == 0);
      if (!cameraConsentDenied || deniedCameraFrames.get() != 0)
        throw new IllegalStateException("Camera consent gate failed");
      try {
        CameraManager camera = context.getSystemService(CameraManager.class);
        inventory.put("logical_camera_count", camera.getCameraIdList().length);
      } catch (Exception unavailable) {
        inventory.put("camera_inventory_error", unavailable.getClass().getSimpleName());
      }
      AtomicInteger events = new AtomicInteger(), failures = new AtomicInteger();
      AtomicLong previous = new AtomicLong();
      try (AndroidGuestSensorSource source =
          new AndroidGuestSensorSource(
              context,
              EnumSet.of(AndroidGuestSensorSource.Kind.ACCELEROMETER),
              reading -> {
                long before = previous.getAndSet(reading.hostTimestampNs());
                if (reading.hostTimestampNs() > before) events.incrementAndGet();
                else failures.incrementAndGet();
              },
              failure -> failures.incrementAndGet())) {
        inventory.put("selected_accelerometer_mask", source.start());
        Thread.sleep(3000);
      }
      int closedCount = events.get();
      Thread.sleep(150);
      inventory.put("accelerometer_events", closedCount);
      inventory.put("source_failures", failures.get());
      inventory.put("events_after_close", events.get() - closedCount);
      inventory.put("microphone_opened", false);
      inventory.put("camera_opened", false);
      inventory.put("speaker_started", false);
      result.putString("inventory", inventory.toString());
      if (failures.get() != 0 || events.get() != closedCount)
        throw new IllegalStateException("Sensor lifetime/ordering failure");
      finish(Activity.RESULT_OK, result);
    } catch (Exception failure) {
      result.putString("error", failure.getClass().getSimpleName() + ": " + failure.getMessage());
      finish(Activity.RESULT_CANCELED, result);
    }
  }
}
