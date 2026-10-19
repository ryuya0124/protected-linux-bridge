/* MIT. Read-only diagnostics for the owned, identity-checked Android guest.
 * No root request, settings write, listener, upload or caller-selected command.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile int stopping;
#include "../native/android_guest_adb.h"

int main(int argc, char **argv) {
    (void)argv;
    if (getuid() || argc != 1)
        return 20;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    if (!output)
        return 21;
    size_t length = 0;
    const char identity[] = "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
                            "getprop ro.build.version.incremental; id -u'";
    const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n";
    if (android_adb(identity, output, &length) || length >= 256) {
        free(output);
        return 22;
    }
    output[length] = 0;
    if (strncmp((char *)output, expected, strlen(expected)) ||
        (strcmp((char *)output + strlen(expected), "0\n") &&
         strcmp((char *)output + strlen(expected), "2000\n"))) {
        free(output);
        return 23;
    }
    const char *const queries[] = {
        ("exec:sh -c 'getprop sys.boot_completed; getprop ro.hardware.egl; "
         "getprop ro.hardware.gltransport; getprop ro.boot.hardware.gltransport; "
         "cat /proc/cmdline; cat /proc/modules; dumpsys SurfaceFlinger'"),
        "exec:logcat -d -b crash -t 200",
        "exec:logcat -d -t 500",
    };
    int result = 0;
    for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
        printf("PLB_ANDROID4G_GUEST_DIAGNOSTIC index=%zu\n", i);
        if (android_adb(queries[i], output, &length)) {
            result = 24;
            break;
        }
        if (fwrite(output, 1, length, stdout) != length) {
            result = 25;
            break;
        }
    }
    free(output);
    return result;
}
