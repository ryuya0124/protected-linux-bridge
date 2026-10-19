/* Exercise the actual command builder, including malformed protocol values. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "android_guest_input.h"

int main(void) {
    char service[256];
    uint32_t values[] = {240, 690, 240, 160, 400};
    assert(!android_input_service(service, sizeof(service), 'S', values));
    assert(strstr(service, "touchscreen swipe 240 690 240 160 400 && echo " ANDROID_INPUT_OK));
    assert(!android_input_service(service, sizeof(service), 'U', values));
    assert(strstr(service, "cmd input keyevent 224 && wm dismiss-keyguard"));
    assert(android_input_service(service, 8, 'U', values));
    values[4] = UINT32_MAX;
    assert(android_input_service(service, sizeof(service), 'S', values));
    values[4] = 400;
    values[0] = 480;
    assert(android_input_service(service, sizeof(service), 'S', values));
    assert(android_input_service(service, sizeof(service), 'T', values));
    values[0] = 3;
    assert(!android_input_service(service, sizeof(service), 'K', values));
    values[0] = 26;
    assert(android_input_service(service, sizeof(service), 'K', values));
    assert(android_input_service(service, sizeof(service), 'Z', values));
    return 0;
}
