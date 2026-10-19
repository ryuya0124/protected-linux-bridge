/* MIT. Fixed guest commands only; numbers are validated before shell formatting.
 * ADB stream closure alone does not prove that Android's input command succeeded.
 */
#define ANDROID_INPUT_OK "PLB_ANDROID_INPUT_OK"

static int android_input_service(char *service, size_t size, unsigned char command,
                                 const uint32_t *values) {
    int length;
    if (command == 'T') {
        if (values[0] >= 480 || values[1] >= 800)
            return -1;
        length =
            snprintf(service, size,
                     "exec:sh -c 'cmd input touchscreen tap %u %u && echo " ANDROID_INPUT_OK "'",
                     values[0], values[1]);
    } else if (command == 'S') {
        if (values[0] >= 480 || values[1] >= 800 || values[2] >= 480 || values[3] >= 800 ||
            values[4] < 50 || values[4] > 1000)
            return -1;
        length = snprintf(
            service, size,
            "exec:sh -c 'cmd input touchscreen swipe %u %u %u %u %u && echo " ANDROID_INPUT_OK "'",
            values[0], values[1], values[2], values[3], values[4]);
    } else if (command == 'K') {
        if (values[0] != 3 && values[0] != 4 && values[0] != 82 && values[0] != 187 &&
            values[0] != 224)
            return -1;
        length =
            snprintf(service, size,
                     "exec:sh -c 'cmd input keyevent %u && echo " ANDROID_INPUT_OK "'", values[0]);
    } else if (command == 'U') {
        /* Dismiss only this verified, non-secure guest's keyguard. No host input,
         * credential bypass, falsing switch or security-policy modification. */
        length = snprintf(service, size,
                          "exec:sh -c 'cmd input keyevent 224 && wm dismiss-keyguard && "
                          "settings put system screen_off_timeout 1800000 && echo " ANDROID_INPUT_OK
                          "'");
    } else {
        return -1;
    }
    return length < 0 || (size_t)length >= size ? -1 : 0;
}
