#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include "preallocated_ready.h"

int main(void) {
    int descriptors[2];
    assert(pipe(descriptors) == 0);
    char text[32];
    snprintf(text, sizeof(text), "%d", descriptors[1]);
    assert(plb_ready_descriptor(text, getuid()) == descriptors[1]);
    snprintf(text, sizeof(text), "%d", descriptors[0]);
    assert(plb_ready_descriptor(text, getuid()) == -1);
    snprintf(text, sizeof(text), "%d", descriptors[1]);
    close(descriptors[1]);
    assert(plb_ready_descriptor(text, getuid()) == -1);
    close(descriptors[0]);
    assert(plb_ready_descriptor("32", getuid()) == -1);
    assert(plb_ready_descriptor("0", getuid()) == -1);
    assert(plb_ready_descriptor("3junk", getuid()) == -1);
    assert(plb_ready_descriptor("9999999999999999999999999999", getuid()) == -1);
    unsetenv(PLB_PREALLOCATED_READY_ENV);
    assert(plb_preallocated_notify_ready() == 0);
    setenv(PLB_PREALLOCATED_READY_ENV, "invalid", 1);
    assert(plb_preallocated_notify_ready() == -1);
    assert(getenv(PLB_PREALLOCATED_READY_ENV) == NULL);
    return 0;
}
