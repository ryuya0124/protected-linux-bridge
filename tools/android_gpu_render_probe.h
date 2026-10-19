/* MIT. Fixed, signed diagnostic APK in the pinned owned guest only.
 * No externally supplied command/path or general-purpose ADB upload endpoint.
 * The builder authenticates the payload; every generated command repeats guest
 * identity checks. Existing Android/Debian host-side disks are never accessed. */
#ifndef PLB_ANDROID_GPU_RENDER_PROBE_H
#define PLB_ANDROID_GPU_RENDER_PROBE_H
#if !defined(PLB_ANDROID_GPU_MEMORY_PROBE)
#error "The render probe requires the isolated, guarded 1536 MiB GLES diagnostic"
#endif
#include <sys/random.h>
#include "android_gpu_probe.generated.h"

static char gpu_probe_nonce[33];

/* A random hex-only nonce is the sole substituted value, not a shell fragment. */
static int gpu_probe_service(const char *text, char output[8192]) {
    size_t used = 0;
    while (*text) {
        if (!strncmp(text, "@NONCE@", 7)) {
            if (used + 32 >= 8192)
                return -1;
            memcpy(output + used, gpu_probe_nonce, 32);
            used += 32;
            text += 7;
        } else {
            if (used + 1 >= 8192)
                return -1;
            output[used++] = *text++;
        }
    }
    output[used] = 0;
    return 0;
}

static int gpu_probe_call(const char *text, unsigned char *output, size_t *length,
                          const char *expected) {
    char service[8192];
    if (stopping || gpu_probe_service(text, service) || android_adb(service, output, length))
        return -1;
    return expected && !memmem(output, *length, expected, strlen(expected)) ? -1 : 0;
}

static int gpu_probe_start(unsigned char *output) {
    unsigned char random[16];
    if (getrandom(random, sizeof(random), 0) != sizeof(random))
        return -1;
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(random); i++) {
        gpu_probe_nonce[i * 2] = digits[random[i] >> 4];
        gpu_probe_nonce[i * 2 + 1] = digits[random[i] & 15];
    }
    size_t length;
    if (gpu_probe_call(gpu_probe_begin, output, &length, "PLB_GPU_APK_BEGIN_OK"))
        return -1; /* The unique directory was not acknowledged as ours. */
    int failed = 0;
    for (size_t i = 0; i < sizeof(gpu_probe_chunks) / sizeof(*gpu_probe_chunks); i++) {
        if (gpu_probe_call(gpu_probe_chunks[i], output, &length, "PLB_GPU_APK_CHUNK_OK")) {
            failed = 1;
            break;
        }
    }
    if (!failed && !gpu_probe_call(gpu_probe_finish, output, &length, "PLB_GPU_APK_STARTED")) {
        fprintf(stderr, "PLB_ANDROID_GPU_PROBE_STARTED nonce=%s\n", gpu_probe_nonce);
        return 0;
    }
    /* Only the two generated temporary names in our acknowledged guest directory.
     * Never recurse or delete a caller-supplied path. */
    (void)gpu_probe_call(gpu_probe_cleanup, output, &length, NULL);
    return -1;
}

static int gpu_probe_save(const char *path, const unsigned char *bytes, size_t length) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    size_t used = 0;
    while (used < length) {
        ssize_t count = write(fd, bytes + used, length - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
    }
    int failed = used != length || fsync(fd);
    close(fd);
    if (failed)
        unlink(path); /* Only the file just created here. */
    return failed ? -1 : 0;
}

/* Capture evidence, never declare GPU success. The offline verifier additionally
 * checks renderer, both GL phases, PNG integrity and the visible nonce texture. */
static int gpu_probe_collect_frame(unsigned char *output) {
    size_t length;
    if (gpu_probe_call(gpu_probe_collect, output, &length, NULL))
        return -1;
    if (length > 8192)
        return -1;
    if (!memmem(output, length, "PLB_GPU_PROBE_RESULT ", 21) ||
        !memmem(output, length, gpu_probe_nonce, 32))
        return 0;
    if (gpu_probe_save(BASE "/android-gpu-probe-result.log", output, length))
        return -1;
    if (android_adb("exec:screencap -p", output, &length) || length < 24 ||
        memcmp(output, "\211PNG\r\n\032\n", 8) ||
        gpu_probe_save(BASE "/android-gpu-probe-frame.png", output, length))
        return -1;
    fprintf(stderr, "PLB_ANDROID_GPU_EVIDENCE_CAPTURED nonce=%s bytes=%zu\n", gpu_probe_nonce,
            length);
    return 1;
}
#endif
