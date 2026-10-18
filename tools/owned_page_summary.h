/* MIT. Aggregate only this process's sampled pages; never expose PFNs.
 * Contiguous base pages are not evidence of a compound/THP folio. The caller
 * must independently establish a 4 KiB host and a 2 MiB-aligned mapping.
 */
#ifndef PLB_OWNED_PAGE_SUMMARY_H
#define PLB_OWNED_PAGE_SUMMARY_H
#include <stdint.h>
#include <stddef.h>

#define PLB_KPF_COMPOUND_HEAD (1ULL << 15)
#define PLB_KPF_COMPOUND_TAIL (1ULL << 16)
#define PLB_KPF_THP (1ULL << 22)

struct plb_page_summary {
    size_t samples, present, unknown_pfn, flag_errors;
    size_t thp_pages, compound_heads, compound_tails;
    size_t current_run, longest_run, reverse_run, longest_reverse_run, sampled_2m_folios;
    uint64_t previous_pfn, window_pfn;
    int window_valid;
};

static void plb_page_observe(struct plb_page_summary *summary, int present, int visible,
                             uint64_t pfn, int flags_valid, uint64_t flags) {
    size_t offset = summary->samples++ % 512;
    if (!offset) {
        summary->window_valid = 1;
        summary->window_pfn = pfn;
    }
    if (present) {
        summary->present++;
    }
    if (!present || !visible) {
        summary->unknown_pfn += present != 0;
        summary->current_run = 0;
        summary->reverse_run = 0;
        summary->window_valid = 0;
        return;
    }
    if (summary->current_run && pfn == summary->previous_pfn + 1) {
        summary->current_run++;
    } else {
        summary->current_run = 1;
    }
    if (summary->reverse_run && pfn + 1 == summary->previous_pfn) {
        summary->reverse_run++;
    } else {
        summary->reverse_run = 1;
    }
    summary->previous_pfn = pfn;
    if (summary->reverse_run > summary->longest_reverse_run) {
        summary->longest_reverse_run = summary->reverse_run;
    }
    if (summary->current_run > summary->longest_run) {
        summary->longest_run = summary->current_run;
    }
    if (!flags_valid) {
        summary->flag_errors++;
        summary->window_valid = 0;
        return;
    }
    int head = !!(flags & PLB_KPF_COMPOUND_HEAD);
    int tail = !!(flags & PLB_KPF_COMPOUND_TAIL);
    int thp = !!(flags & PLB_KPF_THP);
    summary->thp_pages += thp;
    summary->compound_heads += head;
    summary->compound_tails += tail;
    if (!thp || (offset ? (!tail || head || pfn != summary->window_pfn + offset)
                        : (!head || tail || pfn % 512))) {
        summary->window_valid = 0;
    }
    if (offset == 511 && summary->window_valid) {
        summary->sampled_2m_folios++;
    }
}
#endif
