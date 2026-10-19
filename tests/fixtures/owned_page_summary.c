/* Synthetic page observations exercise the real aggregate classifier. No PFNs
 * are read from the machine running this test, and no huge pages are allocated.
 */
#include "owned_page_summary.h"
#include <assert.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    struct plb_page_summary summary = {0};
    int base_only = !strcmp(argv[1], "base-contiguous");
    int short_range = !strcmp(argv[1], "short-range");
    int partial_compound = !strcmp(argv[1], "small-compound");
    size_t count = short_range || partial_compound ? 16 : 512;
    if (!strcmp(argv[1], "two-folios")) {
        count = 1024;
    }
    for (size_t i = 0; i < count; i++) {
        uint64_t pfn = 4096 + i;
        uint64_t flags = i % 512 ? PLB_KPF_COMPOUND_TAIL : PLB_KPF_COMPOUND_HEAD;
        flags |= PLB_KPF_THP;
        int present = 1, visible = 1, flags_valid = 1;
        if (!strcmp(argv[1], "reverse-base")) {
            pfn = 4096 + 511 - i;
            flags = 0;
        }
        if (base_only) {
            flags = 0;
        }
        if (partial_compound) {
            flags &= ~PLB_KPF_THP;
        }
        if (!strcmp(argv[1], "misaligned")) {
            pfn++;
        }
        if (!strcmp(argv[1], "broken-run") && i == 511) {
            pfn += 8;
        }
        if (!strcmp(argv[1], "hidden") && i == 255) {
            visible = 0;
        }
        if (!strcmp(argv[1], "missing") && i == 255) {
            present = 0;
        }
        if (!strcmp(argv[1], "unreadable-flags") && i == 0) {
            flags_valid = 0;
        }
        plb_page_observe(&summary, present, visible, pfn, flags_valid, flags);
    }
    assert(summary.samples == count);
    int complete_thp = !base_only && strcmp(argv[1], "reverse-base") && !partial_compound &&
                       strcmp(argv[1], "hidden") && strcmp(argv[1], "missing") &&
                       strcmp(argv[1], "unreadable-flags");
    assert(plb_page_all_thp(&summary, count) == !!complete_thp);
    assert(!plb_page_all_thp(&summary, count + 1));
    assert(!plb_page_all_thp(&summary, 0));
    if (!strcmp(argv[1], "full-folio")) {
        assert(summary.sampled_2m_folios == 1);
        assert(summary.compound_heads == 1 && summary.compound_tails == 511);
    } else if (!strcmp(argv[1], "two-folios")) {
        assert(summary.sampled_2m_folios == 2);
    } else {
        assert(summary.sampled_2m_folios == 0);
    }
    if (!strcmp(argv[1], "reverse-base")) {
        assert(summary.longest_run == 1 && summary.longest_reverse_run == 512);
    }
    if (base_only) {
        assert(summary.longest_run == 512 && summary.thp_pages == 0);
    }
    if (!strcmp(argv[1], "hidden")) {
        assert(summary.unknown_pfn == 1 && summary.longest_run == 256);
    }
    if (!strcmp(argv[1], "unreadable-flags")) {
        assert(summary.flag_errors == 1);
    }
    return 0;
}
