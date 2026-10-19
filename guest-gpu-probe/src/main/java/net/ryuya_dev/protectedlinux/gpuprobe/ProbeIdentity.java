package net.ryuya_dev.protectedlinux.gpuprobe;

/** Check before constructing GLSurfaceView; a host install must not create this EGL workload. */
final class ProbeIdentity {
  private ProbeIdentity() {}

  static boolean allowed(String device, String hardware, String build, String nonce) {
    return "vsoc_arm64_only".equals(device)
        && "cutf_cvm".equals(hardware)
        && "16373615".equals(build)
        && nonce != null
        && nonce.matches("[0-9a-f]{32}");
  }
}
