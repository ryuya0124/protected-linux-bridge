/* MIT. Fixed guest-only HAL readback payload; no host hardware access.
 * The build selects one embedded service. This executable has no caller argv,
 * guest address, shell command or arbitrary upload interface.
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
#include "android_guest_adb.h"
#ifndef PLB_GUEST_CHECK_HEADER
#error "Select the built-in guest HAL check header at compile time"
#endif
#include PLB_GUEST_CHECK_HEADER
int main(void) {
    if (getuid())
        return 20;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    size_t length = 0;
    const char identity[] =
        "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
        "getprop ro.build.version.incremental; getprop ro.build.version.sdk; id -u'";
    const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n37\n0\n";
    if (!output || android_adb(identity, output, &length) || length != sizeof(expected) - 1 ||
        memcmp(output, expected, length))
        return 21;
    int failed = android_adb(plb_guest_check_service, output, &length);
    if (!memmem(output, length, plb_guest_check_pass, sizeof(plb_guest_check_pass) - 1))
        failed = 1;
    fwrite(output, 1, length, stdout);
    free(output);
    return failed ? 22 : 0;
}
