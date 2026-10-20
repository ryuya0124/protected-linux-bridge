package net.ryuya_dev.protectedlinux.guestprobe;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/** Guest-only silent PCM test. The separately pinned VMM backend is null, with no capture. */
public final class GuestAudioReadback {
  private static int openGuestPlaybackDescriptors() throws IOException {
    int opened = 0;
    try (var processes = Files.list(Path.of("/proc"))) {
      for (Path process :
          processes.filter(path -> path.getFileName().toString().matches("[0-9]+")).toList()) {
        try (var descriptors = Files.list(process.resolve("fd"))) {
          for (Path descriptor : descriptors.toList()) {
            try {
              if (Files.readSymbolicLink(descriptor).toString().equals("/dev/snd/pcmC0D0p"))
                opened++;
            } catch (IOException closedDuringRead) {
              // Guest processes can close descriptors while the snapshot is taken.
            }
          }
        } catch (IOException unavailableProcess) {
          // A guest process may have exited. An unreadable process is not a hit.
        }
      }
    }
    return opened;
  }

  public static void main(String[] arguments) throws Exception {
    System.out.println("PLB_GUEST_AUDIO_STAGE java_started");
    String cards = Files.readString(Path.of("/proc/asound/cards"));
    if (!cards.contains("VirtIO")) throw new IllegalStateException("Virtio sound card missing");
    System.out.println("PLB_GUEST_AUDIO_STAGE card_verified");
    int minimum =
        AudioTrack.getMinBufferSize(
            48000, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
    if (minimum <= 0) throw new IllegalStateException("Guest audio minimum buffer");
    System.out.println("PLB_GUEST_AUDIO_STAGE buffer_queried");
    AudioTrack track =
        new AudioTrack.Builder()
            .setAudioAttributes(
                new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build())
            .setAudioFormat(
                new AudioFormat.Builder()
                    .setSampleRate(48000)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .build())
            .setBufferSizeInBytes(Math.max(minimum * 2, 19200))
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build();
    System.out.println("PLB_GUEST_AUDIO_STAGE track_created");
    int written = 0, head;
    int playbackDescriptors;
    try {
      if (track.getState() != AudioTrack.STATE_INITIALIZED)
        throw new IllegalStateException("Guest AudioTrack not initialized");
      track.play();
      byte[] silence = new byte[19200];
      for (int i = 0; i < 8; i++) {
        int count = track.write(silence, 0, silence.length, AudioTrack.WRITE_BLOCKING);
        if (count != silence.length) throw new IllegalStateException("Guest PCM write failed");
        written += count;
      }
      Thread.sleep(100);
      head = track.getPlaybackHeadPosition();
      // CONFIG_SND_VERBOSE_PROCFS is optional. Prove the HAL opened the real
      // guest PCM device rather than treating a missing proc debug file as failure.
      playbackDescriptors = openGuestPlaybackDescriptors();
    } finally {
      track.stop();
      track.release();
    }
    System.out.println("PLB_GUEST_AUDIO_READBACK bytes=" + written + " playback_frames=" + head);
    System.out.println("PLB_GUEST_AUDIO_ALSA_OPEN_DESCRIPTORS=" + playbackDescriptors);
    boolean passed = written == 153600 && head > 0 && playbackDescriptors > 0;
    System.out.println(passed ? "PLB_GUEST_AUDIO_HAL_PASS" : "PLB_GUEST_AUDIO_HAL_FAILED");
    System.exit(passed ? 0 : 1);
  }
}
