package net.ryuya_dev.protectedlinux;

import android.app.*;
import android.content.*;
import android.content.pm.*;
import android.os.IBinder;

/** Explicit opt-in background microphone; visible OS microphone FGS, no root capture. */
public final class MicrophoneService extends Service {
  private static final int NOTICE = 42;
  private static final String CHANNEL = "linux_microphone";
  private AudioBridge audio;
  static volatile boolean running;

  private void state(int state) {
    getSharedPreferences("config", 0).edit().putInt("microphone_state", state).apply();
  }

  static void apply(Context context) {
    var p = context.getSharedPreferences("config", 0);
    boolean on =
        p.getBoolean("enabled", true)
            && p.getBoolean("microphone_enabled", false)
            && p.getBoolean("microphone_background", false)
            && context.checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                == PackageManager.PERMISSION_GRANTED;
    if (!on) {
      context.stopService(new Intent(context, MicrophoneService.class));
      return;
    }
    try {
      context.startForegroundService(new Intent(context, MicrophoneService.class));
    } catch (RuntimeException error) {
      p.edit().putInt("microphone_state", 3).apply();
      android.util.Log.w(
          "ProtectedLinux", "MICROPHONE_FGS_REJECTED " + error.getClass().getSimpleName());
      android.widget.Toast.makeText(
              context, R.string.microphone_start_failed, android.widget.Toast.LENGTH_LONG)
          .show();
    }
  }

  static void reconfigure(Context context) {
    if (running)
      context.startService(new Intent(context, MicrophoneService.class).setAction("RECONFIGURE"));
  }

  @Override
  public void onCreate() {
    super.onCreate();
    getSystemService(NotificationManager.class)
        .createNotificationChannel(
            new NotificationChannel(
                CHANNEL, getString(R.string.microphone_mode), NotificationManager.IMPORTANCE_LOW));
  }

  private Notification notice(String text) {
    Intent stop = new Intent(this, MicrophoneService.class).setAction("STOP");
    PendingIntent stopAction =
        PendingIntent.getService(
            this, 1, stop, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    PendingIntent open =
        PendingIntent.getActivity(
            this,
            2,
            new Intent(this, MainActivity.class),
            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    return new Notification.Builder(this, CHANNEL)
        .setSmallIcon(R.drawable.ic_terminal)
        .setContentTitle(getString(R.string.microphone_service))
        .setContentText(text)
        .setContentIntent(open)
        .setOngoing(true)
        .addAction(
            new Notification.Action.Builder(null, getString(R.string.stop_microphone), stopAction)
                .build())
        .build();
  }

  @Override
  public int onStartCommand(Intent intent, int flags, int id) {
    if (intent != null && "STOP".equals(intent.getAction())) {
      getSharedPreferences("config", 0).edit().putBoolean("microphone_enabled", false).apply();
      stopSelf();
      return START_NOT_STICKY;
    }
    var p = getSharedPreferences("config", 0);
    if (!p.getBoolean("enabled", true)
        || !p.getBoolean("microphone_enabled", false)
        || !p.getBoolean("microphone_background", false)
        || checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED) {
      stopSelf();
      return START_NOT_STICKY;
    }
    if (intent != null && "RECONFIGURE".equals(intent.getAction()) && audio != null) {
      audio.close();
      audio = null;
    }
    try {
      startForeground(
          NOTICE,
          notice(getString(R.string.checking)),
          ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
      if (audio == null) {
        state(1);
        audio =
            new AudioBridge(
                this,
                message ->
                    getSystemService(NotificationManager.class).notify(NOTICE, notice(message)),
                () -> {
                  state(3);
                  stopSelf();
                },
                () -> state(2));
        audio.start(false, true);
      }
      running = true;
    } catch (RuntimeException error) {
      state(3);
      android.util.Log.w(
          "ProtectedLinux", "MICROPHONE_SERVICE_STOP " + error.getClass().getSimpleName());
      stopSelf();
    }
    return START_NOT_STICKY;
  }

  @Override
  public void onDestroy() {
    running = false;
    if (getSharedPreferences("config", 0).getInt("microphone_state", 0) != 3) state(0);
    if (audio != null) audio.close();
    stopForeground(STOP_FOREGROUND_REMOVE);
    super.onDestroy();
  }

  @Override
  public IBinder onBind(Intent intent) {
    return null;
  }
}
