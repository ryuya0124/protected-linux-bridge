#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "android_guest_frame.h"

int main(void) {
    unsigned char *frame = calloc(1, ANDROID_RAW_FRAME_SIZE);
    assert(frame);
    uint32_t header[] = {480, 800, 1, 1};
    memcpy(frame, header, sizeof(header));
    assert(android_raw_frame_valid(frame, ANDROID_RAW_FRAME_SIZE));
    assert(!android_raw_frame_valid(frame, ANDROID_RAW_FRAME_SIZE - 1));
    assert(!android_raw_frame_valid(frame, 0));
    for (unsigned i = 0; i < 4; i++) {
        memcpy(frame, header, sizeof(header));
        memset(frame + i * 4, 0xff, 4);
        assert(!android_raw_frame_valid(frame, ANDROID_RAW_FRAME_SIZE));
    }
    free(frame);
    return 0;
}
