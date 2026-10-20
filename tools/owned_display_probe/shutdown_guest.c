/* MIT. Fixed guest-CID shutdown after checking the Cuttlefish identity. No host reboot API. */
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
int main(void) {
    if (getuid())
        return 20;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    size_t length = 0;
    const char identity[] = "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
                            "getprop ro.build.version.incremental; id -u'";
    const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n0\n";
    if (!output || android_adb(identity, output, &length) || length != sizeof(expected) - 1 ||
        memcmp(output, expected, length))
        return 21;
    int failed = android_adb("exec:svc power shutdown", output, &length);
    fwrite(output, 1, length, stdout);
    free(output);
    printf("ANDROID17_OWNED_GUEST_SHUTDOWN_REQUESTED %d\n", !failed);
    return failed ? 22 : 0;
}
