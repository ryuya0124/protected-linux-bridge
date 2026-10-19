/* MIT. Fixed guest network setup and read-only state queries; no command API. */
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
#include "../../native/android_guest_adb.h"
#include "android_network_inspect.generated.h"

int main(int argc, char **argv) {
    if (getuid() || argc > 2)
        return 20;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT);
    if (!output)
        return 21;
    int result = 0;
    size_t length;
    if (argc == 2) {
        if (strcmp(argv[1], "--state") && strcmp(argv[1], "--launch") &&
            strcmp(argv[1], "--verify")) {
            free(output);
            return 20;
        }
        result = android_adb(!strcmp(argv[1], "--state")    ? network_trial_state
                             : !strcmp(argv[1], "--verify") ? network_trial_verify
                                                            : network_trial_launch,
                             output, &length);
        if (!result) {
            fwrite(output, 1, length, stdout);
            if (!memmem(output, length, "PLB_NETWORK_INSPECT_STEP_OK", 27))
                result = 22;
        }
    } else {
        for (size_t i = 0; i < sizeof(network_inspect_commands) / sizeof(*network_inspect_commands);
             i++) {
            int transport = android_adb(network_inspect_commands[i], output, &length);
            if (transport || !memmem(output, length, "PLB_NETWORK_INSPECT_STEP_OK", 27)) {
                fprintf(stdout, "PLB_NETWORK_TRIAL_FAILED step=%zu transport=%d bytes=%zu\n", i,
                        transport, length);
                fwrite(output, 1, length, stdout);
                result = 22;
                break;
            }
            if (i + 1 == sizeof(network_inspect_commands) / sizeof(*network_inspect_commands))
                fwrite(output, 1, length, stdout);
        }
    }
    free(output);
    return result;
}
