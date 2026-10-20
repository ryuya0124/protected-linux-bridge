/* MIT. Fixed, signed guest-only workload through the pinned owned guest's adbd.
 * No host ADB, arbitrary payload/path, Vulkan or general shell endpoint. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static volatile int stopping;
#include "android_guest_adb.h"
#include "motion_guest.generated.h"
int main(int argc, char **argv) {
    if (getuid() || argc != 2)
        return 20;
    int start = !strcmp(argv[1], "--start");
    const char *service = !strcmp(argv[1], "--status") ? motion_status : motion_stop;
    if (!start && strcmp(argv[1], "--status") && strcmp(argv[1], "--stop"))
        return 21;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    size_t length = 0;
    const char identity[] =
        "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
        "getprop ro.build.version.incremental; getprop ro.build.version.sdk; id -u'";
    const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n37\n0\n";
    if (!output || android_adb(identity, output, &length) || length != sizeof(expected) - 1 ||
        memcmp(output, expected, length))
        return 22;
    puts("PLB_MOTION_GUEST_IDENTITY sdk=37 hardware=cutf_cvm root=1");
    if (start) {
        if (android_adb(motion_begin, output, &length) ||
            !memmem(output, length, "PLB_MOTION_BEGIN_OK", 19))
            return 24;
        for (size_t i = 0; i < sizeof(motion_chunks) / sizeof(*motion_chunks); i++)
            if (android_adb(motion_chunks[i], output, &length) ||
                !memmem(output, length, "PLB_MOTION_CHUNK_OK", 19))
                return 25;
        service = motion_start;
    }
    int failed = android_adb(service, output, &length);
    if (start && !memmem(output, length, "PLB_MOTION_STARTED", 18))
        failed = 1;
    fwrite(output, 1, length, stdout);
    free(output);
    return failed ? 23 : 0;
}
