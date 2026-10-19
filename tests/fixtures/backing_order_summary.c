/* MIT. Synthetic page order; no physical addresses or device interaction. */
#include "backing_order_summary.h"
#include <assert.h>

int main(void) {
    const uint64_t present = 1ULL << 63;
    struct plb_backing_order forward = {0}, reverse = {0}, misaligned = {0};
    for (uint64_t i = 0; i < 16; i++) {
        plb_backing_order_observe(&forward, present | (16 + i));
        plb_backing_order_observe(&reverse, present | (31 - i));
        plb_backing_order_observe(&misaligned, present | (17 + i));
    }
    assert(forward.present == 16 && forward.longest_forward == 16);
    assert(forward.aligned_16k_forward == 4 && forward.aligned_64k_forward == 1);
    assert(reverse.longest_reverse == 16 && reverse.longest_forward == 1);
    assert(!reverse.aligned_16k_forward && !reverse.aligned_64k_forward);
    assert(misaligned.longest_forward == 16 && !misaligned.aligned_16k_forward);
    assert(!misaligned.aligned_64k_forward);

    struct plb_backing_order broken = {0};
    for (uint64_t i = 0; i < 16; i++)
        plb_backing_order_observe(&broken, i == 1 ? 0 : present | (16 + i));
    assert(broken.present == 15 && broken.aligned_16k_forward == 3);
    assert(!broken.aligned_64k_forward && broken.longest_forward == 14);
    plb_backing_order_observe(&broken, present);
    assert(broken.hidden == 1 && !broken.forward_run && !broken.reverse_run);
    return 0;
}
