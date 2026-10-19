/* MIT. Send only the source-built metadata inspector to the pinned guest.
 * Fixed generated payload/path; no general upload API, network or host writes.
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
#define PLB_ANDROID_ADB_TRACE 1
#include "../native/android_guest_adb.h"
#include "android_network_inspect.generated.h"

int main(int argc, char **argv) {
    (void)argv;
    if (getuid() || argc != 1)
        return 20;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT);
    if (!output)
        return 21;
    int result = 0;
    size_t length;
    for (size_t i = 0; i < sizeof(network_inspect_commands) / sizeof(*network_inspect_commands);
         i++) {
        int transport = android_adb(network_inspect_commands[i], output, &length);
        if (transport || !memmem(output, length, "PLB_NETWORK_INSPECT_STEP_OK", 27)) {
            fprintf(stdout, "PLB_NETWORK_INSPECT_FAILED step=%zu transport=%d bytes=%zu\n", i,
                    transport, length);
            fwrite(output, 1, length, stdout);
            result = 22;
            break;
        }
        if (i + 1 == sizeof(network_inspect_commands) / sizeof(*network_inspect_commands))
            fwrite(output, 1, length, stdout);
    }
    free(output);
    return result;
}
