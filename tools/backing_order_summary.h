/* MIT. Owned backing-page order statistics; never publishes a PFN. */
#ifndef PLB_BACKING_ORDER_SUMMARY_H
#define PLB_BACKING_ORDER_SUMMARY_H
#include <stddef.h>
#include <stdint.h>

struct plb_backing_order {
    size_t samples, present, hidden;
    size_t forward_run, reverse_run, longest_forward, longest_reverse;
    size_t aligned_16k_forward, aligned_64k_forward;
    uint64_t previous, window4, window16;
    int valid4, valid16;
};

static void plb_backing_order_observe(struct plb_backing_order *summary, uint64_t entry) {
    size_t index = summary->samples++;
    int present = !!(entry & (1ULL << 63));
    uint64_t pfn = entry & ((1ULL << 55) - 1);
    int visible = present && pfn;
    summary->present += present;
    summary->hidden += present && !pfn;
    if (!(index % 4)) {
        summary->window4 = pfn;
        summary->valid4 = visible && !(pfn % 4);
    }
    if (!(index % 16)) {
        summary->window16 = pfn;
        summary->valid16 = visible && !(pfn % 16);
    }
    summary->valid4 &= visible && pfn == summary->window4 + index % 4;
    summary->valid16 &= visible && pfn == summary->window16 + index % 16;
    if (index % 4 == 3 && summary->valid4)
        summary->aligned_16k_forward++;
    if (index % 16 == 15 && summary->valid16)
        summary->aligned_64k_forward++;
    if (!visible) {
        summary->forward_run = summary->reverse_run = 0;
        return;
    }
    summary->forward_run =
        summary->forward_run && pfn == summary->previous + 1 ? summary->forward_run + 1 : 1;
    summary->reverse_run =
        summary->reverse_run && pfn + 1 == summary->previous ? summary->reverse_run + 1 : 1;
    if (summary->forward_run > summary->longest_forward)
        summary->longest_forward = summary->forward_run;
    if (summary->reverse_run > summary->longest_reverse)
        summary->longest_reverse = summary->reverse_run;
    summary->previous = pfn;
}
#endif
