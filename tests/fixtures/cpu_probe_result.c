#include "cpu_probe_result.h"
#include <assert.h>

static int check(const char *log, int debian) {
    FILE *file = tmpfile();
    assert(file);
    fputs(log, file);
    rewind(file);
    int result = plb_cpu_console_success(file, "0123456789abcdef0123456789abcdef", debian);
    fclose(file);
    return result;
}

#define MEMORY                                                                                     \
    "PLB_CPU_GUEST nonce=0123456789abcdef0123456789abcdef result=0 arch=aarch64 release=6.6.118 "  \
    "bytes=16777216\r\n"
#define END "PLB_CPU_FINISHED nonce=0123456789abcdef0123456789abcdef result=0\r\n"

int main(void) {
    assert(check(MEMORY END, 0));
    assert(!check("exiting with success\n", 0));
    assert(!check(END, 0));
    assert(!check(MEMORY, 0));
    assert(!check(END MEMORY, 0));
    assert(!check(MEMORY END, 1));
    assert(check(MEMORY "arm64\nPLB_CPU_DEBIAN_READONLY result=0\n" END, 1));
    assert(!check("PLB_CPU_GUEST nonce=ffffffffffffffffffffffffffffffff result=0 arch=aarch64 "
                  "release=x bytes=16777216\n" END,
                  0));
    assert(!check("PLB_CPU_GUEST nonce=0123456789abcdef0123456789abcdef result=0 arch=x86_64 "
                  "release=x bytes=16777216\n" END,
                  0));
    assert(!check(MEMORY "PLB_CPU_GUEST nonce=0123456789abcdef0123456789abcdef result=1 "
                         "arch=aarch64 release=x bytes=16777216\n" END,
                  0));
    assert(!check(MEMORY "PLB_CPU_FINISHED nonce=0123456789abcdef0123456789abcdef result=1\n", 0));
}
