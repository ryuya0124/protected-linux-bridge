/* MIT. Fixed owned guest HTTPS/default-network probe. No host input or network changes. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static volatile int stopping;
#define PLB_ANDROID_ADB_TIMEOUT_SECONDS 40
#include "android_guest_adb.h"
#include "android_network_inspect.generated.h"
int main(void) {
    if (getuid())
        return 20;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT);
    if (!output)
        return 21;
    size_t length;
    int failed = android_adb(network_trial_connectivity, output, &length);
    if (!failed)
        fwrite(output, 1, length, stdout);
    int passed = !failed &&
                 memmem(output, length, "PLB_GUEST_HTTPS_COMPLETE passed=3 total=3", 41) &&
                 memmem(output, length, "PLB_NETWORK_INSPECT_STEP_OK", 27);
    free(output);
    return passed ? 0 : 22;
}
