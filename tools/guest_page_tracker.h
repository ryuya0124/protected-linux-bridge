/* MIT. Guest-only pagemap aggregation. No physical addresses are emitted. */
#ifndef PLB_GUEST_PAGE_TRACKER_H
#define PLB_GUEST_PAGE_TRACKER_H
#include <stddef.h>
#include <stdint.h>

struct plb_guest_page_scan {
    size_t samples, present, absent, hidden, outside, new_pages;
};

/* seen has ceil(pfn_limit / 8) bytes. The caller owns both the bitmap and
 * pagemap entries. Zero PFNs are treated as unavailable, not a valid page on
 * this diagnostic's guest RAM layout (whose base is 2 GiB). */
static void plb_guest_page_observe(struct plb_guest_page_scan *scan, uint64_t entry,
                                   unsigned char *seen, size_t pfn_limit) {
    scan->samples++;
    if (!(entry & (1ULL << 63))) {
        scan->absent++;
        return;
    }
    scan->present++;
    uint64_t pfn = entry & ((1ULL << 55) - 1);
    if (!pfn) {
        scan->hidden++;
        return;
    }
    if (pfn >= pfn_limit) {
        scan->outside++;
        return;
    }
    unsigned char mask = (unsigned char)(1U << (pfn % 8));
    if (!(seen[pfn / 8] & mask)) {
        seen[pfn / 8] |= mask;
        scan->new_pages++;
    }
}
/* Only relative 256 MiB guest-RAM bands are emitted, never host or guest PFNs.
 * These fixed guests start RAM at 2 GiB and reserve no RAM above 32 GiB. */
#define PLB_GUEST_RAM_BANDS 120
static inline int plb_guest_page_band(uint64_t entry, size_t page_size) {
    if (!(entry & (1ULL << 63)) || (page_size != 4096 && page_size != 16384))
        return -1;
    uint64_t pfn = entry & ((1ULL << 55) - 1);
    if (pfn < (2ULL << 30) / page_size || pfn >= (32ULL << 30) / page_size)
        return -1;
    return (int)((pfn * page_size - (2ULL << 30)) / (256ULL << 20));
}
#endif
