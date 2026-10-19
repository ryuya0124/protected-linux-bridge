#include <assert.h>
#include <string.h>
#include "full_memory_pattern.h"

int main(void) {
    uint64_t first[512], second[512];
    plb_full_store(first, 0, 1);
    plb_full_store(second, 4096, 1);
    assert(plb_full_check(first, 0, 1) == 0);
    assert(plb_full_check(second, 4096, 1) == 0);
    second[511] ^= UINT64_C(0x8000000000000000);
    assert(plb_full_check(second, 4096, 1) == 1);
    memcpy(second, first, sizeof(first));
    assert(plb_full_check(second, 4096, 1) == 512);
    assert(plb_full_check(first, 0, 2) == 512);
    plb_full_store(second, 4096UL * 1048575, 1);
    assert(plb_full_check(second, 4096UL * 1048575, 1) == 0);
    return 0;
}
