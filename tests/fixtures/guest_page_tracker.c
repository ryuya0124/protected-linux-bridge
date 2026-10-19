/* MIT. Synthetic guest entries only; never reads host pagemap or starts a VM. */
#include "guest_page_tracker.h"
#include <assert.h>

int main(void) {
    unsigned char guarded[] = {0xA5, 0, 0, 0x5A};
    unsigned char *seen = guarded + 1;
    const uint64_t present = 1ULL << 63;
    struct plb_guest_page_scan first = {0};
    plb_guest_page_observe(&first, present | 3, seen, 16);
    plb_guest_page_observe(&first, present | 4, seen, 16);
    plb_guest_page_observe(&first, present | 3, seen, 16);
    assert(first.samples == 3 && first.present == 3 && first.new_pages == 2);

    struct plb_guest_page_scan second = {0};
    plb_guest_page_observe(&second, present | 3, seen, 16);
    plb_guest_page_observe(&second, present | 4, seen, 16);
    plb_guest_page_observe(&second, present | 8 | (1ULL << 55), seen, 16);
    assert(second.present == 3 && second.new_pages == 1);

    struct plb_guest_page_scan invalid = {0};
    plb_guest_page_observe(&invalid, 0, seen, 16);
    plb_guest_page_observe(&invalid, (1ULL << 62) | 10, seen, 16);
    plb_guest_page_observe(&invalid, present, seen, 16);
    plb_guest_page_observe(&invalid, present | 16, seen, 16);
    plb_guest_page_observe(&invalid, present | ((1ULL << 55) - 1), seen, 16);
    assert(invalid.samples == 5 && invalid.absent == 2 && invalid.present == 3);
    assert(invalid.hidden == 1 && invalid.outside == 2 && invalid.new_pages == 0);
    assert(guarded[0] == 0xA5 && guarded[3] == 0x5A);
    assert(seen[0] == ((1U << 3) | (1U << 4)) && seen[1] == 1);
    return 0;
}
