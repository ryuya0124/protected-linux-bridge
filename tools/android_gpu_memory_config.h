/* MIT. Development-only 1536 MiB GLES profile; never used by the APK.
 * Separate writable disks, exact memory/renderer arguments and a pinned
 * prefault-only library keep the GPU comparison independent of software runs. */
#ifndef PLB_ANDROID_GPU_MEMORY_CONFIG_H
#define PLB_ANDROID_GPU_MEMORY_CONFIG_H
#define PLB_GPU_MEMORY_MIB 1536
#define PLB_GPU_MEMORY_DIRECTORY "cf17-gpu1536-gles"
#define PLB_GPU_MEMORY_LIBRARY                                                                     \
    "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/"                               \
    "libandroid_gpu1536_gles_backing.so"
#define PLB_GPU_MEMORY_ARGUMENT "size=1536"
#define PLB_GPU_MEMORY_GPU_ARGUMENT                                                                \
    "backend=gfxstream,context-types=gfxstream-gles:gfxstream-composer,"                           \
    "egl=true,gles=true,vulkan=false,glx=false,surfaceless=true,width=480,height=800"
#define PLB_GPU_MEMORY_BACKING_BYTES (1504UL * 1024 * 1024)
#endif
