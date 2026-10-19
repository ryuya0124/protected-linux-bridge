#include <assert.h>
#include "preallocated_vm_limits.h"

int main(void) {
    assert(plb_preallocated_workload(512, 256));
    assert(plb_preallocated_workload(4480, 4096));
    assert(plb_preallocated_workload(5120, 4096));
    assert(plb_preallocated_workload_mode(5888, 4096, 0));
    assert(!plb_preallocated_workload_mode(5888, 4096, 1));
    assert(plb_preallocated_workload_mode(5120, 4096, 1));
    assert(!plb_preallocated_workload(6144, 4096));
    assert(!plb_preallocated_workload(8192, 4096));
    assert(!plb_preallocated_workload(512, 4096));
    assert(!plb_preallocated_workload(4096, 4096));
    assert(!plb_preallocated_workload(-1, 0));
    int memory = 0, touch = 0;
    assert(plb_preallocated_policy("--preallocated-2048-1536", &memory, &touch));
    assert(memory == 2048 && touch == 1536);
    assert(!plb_preallocated_policy_mode("--preallocated-normal-first-pin-5888-4096", &memory,
                                         &touch, 3));
    assert(!plb_preallocated_policy("--preallocated-2048-1536-extra", &memory, &touch));
    assert(!plb_preallocated_policy("--preallocated-9999999999999999999999-256", &memory, &touch));
    assert(
        plb_preallocated_policy_mode("--preallocated-normal-first-3072-2048", &memory, &touch, 2));
    const size_t mib = 1024UL * 1024, batch = 16 * mib;
    unsigned char seen[192] = {0};
    for (size_t i = 0; i < 192; i++) {
        size_t offset = plb_preallocated_offset(i, 3072 * mib, batch, 1);
        assert(offset < 3072 * mib && offset % batch == 0);
        assert(!seen[offset / batch]);
        seen[offset / batch] = 1;
    }
    assert(plb_preallocated_offset(0, 3072 * mib, batch, 1) == 2048 * mib);
    assert(plb_preallocated_offset(64, 3072 * mib, batch, 1) == 0);
    assert(plb_preallocated_offset(192, 3072 * mib, batch, 1) == (size_t)-1);
    return 0;
}
