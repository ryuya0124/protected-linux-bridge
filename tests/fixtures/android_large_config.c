/* MIT. Compile-time preset identity used by both the manager and backing library. */
#include "android_large_config.h"
#include <stdio.h>

int main(void) {
    printf("%d\n%s\n%s\n%s%s\n%lu\n%lu\n", PLB_ANDROID_LARGE_MEMORY_MIB, PLB_LARGE_DIRECTORY,
           PLB_LARGE_LIBRARY_NAME, PLB_LARGE_READY_MARKER, PLB_LARGE_MEMORY_ARGUMENT,
           PLB_LARGE_BACKING_BYTES, PLB_LARGE_HEADROOM_KIB);
    return 0;
}
