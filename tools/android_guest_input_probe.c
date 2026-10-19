/* MIT. Developer-only input diagnosis for the owned, identity-checked guest.
 * No arbitrary command/path, host input, root request or VM startup endpoint.
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
#include "../native/android_guest_input.h"

int main(int argc, char **argv) {
    if (getuid() || argc != 2 ||
        (strcmp(argv[1], "--state") && strcmp(argv[1], "--unlock") && strcmp(argv[1], "--swipe")))
        return 20;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    if (!output)
        return 21;
    size_t length;
    const char identity[] = "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
                            "getprop ro.build.version.incremental; id -u'";
    const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n0\n";
    int result = 22;
    if (android_adb(identity, output, &length) || length != sizeof(expected) - 1 ||
        memcmp(output, expected, sizeof(expected) - 1))
        goto done;
    char service[256];
    if (strcmp(argv[1], "--state")) {
        uint32_t swipe[] = {240, 690, 240, 160, 400};
        if (android_input_service(service, sizeof(service), !strcmp(argv[1], "--swipe") ? 'S' : 'U',
                                  swipe))
            goto done;
        result = android_adb(service, output, &length) ||
                 !memmem(output, length, ANDROID_INPUT_OK, sizeof(ANDROID_INPUT_OK) - 1);
        fwrite(output, 1, length, stdout);
    } else {
        const char *queries[] = {
            "exec:sh -c 'timeout 3 dumpsys window policy; timeout 3 dumpsys power'",
            "exec:sh -c 'logcat -d -v brief -s FalsingManager:V BrightLineFalsingManager:V "
            "FalsingCollector:V FalsingClassifier:V KeyguardViewMediator:V "
            "KeyguardUpdateMonitor:V PowerManagerService:I \"*:S\" | tail -n 160'",
        };
        result = 0;
        for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
            if (android_adb(queries[i], output, &length))
                result = 23;
            fwrite(output, 1, length, stdout);
        }
    }
done:
    free(output);
    return result;
}
