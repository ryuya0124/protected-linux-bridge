/* MIT. Guest-only bootstrap and observation of the fixed isolated Android guest.
 * Does not expose a socket or accept arbitrary shell commands. Every sample is
 * tied to the pinned guest identity before setup or a private PNG is retained. */
#ifndef PLB_ANDROID_MEMORY_OBSERVER_H
#define PLB_ANDROID_MEMORY_OBSERVER_H
#include <pthread.h>
#include "../native/android_guest_adb.h"
#include "android_guest_setup.generated.h"

static void *android_memory_observer(void *unused) {
    (void)unused;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    if (!output)
        return NULL;
    const char query[] = "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
                         "getprop ro.build.version.incremental; id -u; "
                         "echo BOOT=$(getprop sys.boot_completed); "
                         "grep -E \"^(MemTotal|MemAvailable):\" /proc/meminfo; "
                         "echo SURFACEFLINGER=$(pidof surfaceflinger); "
                         "echo SYSTEMUI=$(pidof com.android.systemui)'";
    const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n";
    int captured = 0, setup = 0;
    for (int tick = 0; tick < 1100 && !stopping; tick++) {
        if (tick % 50) {
            usleep(100000);
            continue;
        }
        size_t length = 0;
        if (android_adb(query, output, &length) || length >= 2048)
            continue;
        output[length] = 0;
        if (strncmp((char *)output, expected, strlen(expected))) {
            fprintf(stderr, "PLB_ANDROID_MEMORY_IDENTITY_REJECTED\n");
            break;
        }
        const char *uid = (char *)output + strlen(expected);
        if (strncmp(uid, "0\n", 2) && strncmp(uid, "2000\n", 5))
            break;
        fprintf(stderr, "PLB_ANDROID_MEMORY_SAMPLE %s\n", uid);
        if (!setup) {
            if (!strncmp(uid, "2000\n", 5)) {
                android_adb("root:", output, &length);
                continue;
            }
            if (android_adb(android_setup_service, output, &length) ||
                !memmem(output, length, "PLB_ANDROID_GUEST_SETUP_READY", 29)) {
                fprintf(stderr, "PLB_ANDROID_MEMORY_SETUP_FAILED\n");
                break;
            }
            setup = 1;
            fprintf(stderr, "PLB_ANDROID_MEMORY_GUEST_SETUP_READY\n");
            continue;
        }
        if (!captured && strstr(uid, "BOOT=1\n")) {
            const char home[] =
                "exec:sh -c 'cmd power wakeup; wm dismiss-keyguard; input keyevent 3; sleep 2; "
                "dumpsys activity activities | grep -E \"mResumedActivity|topResumedActivity\"'";
            if (!android_adb(home, output, &length) && length < 2048) {
                output[length] = 0;
                fprintf(stderr, "PLB_ANDROID_MEMORY_HOME %s\n", output);
            }
            if (!android_adb("exec:screencap -p", output, &length) && length >= 24 &&
                !memcmp(output, "\211PNG\r\n\032\n", 8)) {
                int fd = open(BASE "/android-memory-frame.png",
                              O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
                if (fd >= 0) {
                    size_t at = 0;
                    while (at < length) {
                        ssize_t count = write(fd, output + at, length - at);
                        if (count < 0 && errno == EINTR)
                            continue;
                        if (count <= 0)
                            break;
                        at += (size_t)count;
                    }
                    captured = at == length && fsync(fd) == 0;
                    close(fd);
                    fprintf(stderr, "PLB_ANDROID_MEMORY_FRAME complete=%d bytes=%zu\n", captured,
                            at);
                }
            }
        }
    }
    free(output);
    return NULL;
}
#endif
