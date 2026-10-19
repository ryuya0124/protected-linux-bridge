/* MIT. Index only the selected middle range, including reverse fault order.
 * Prefix/tail boot margins are populated separately before this traversal. */
#ifndef PLB_BACKING_PAGE_ORDER_H
#define PLB_BACKING_PAGE_ORDER_H
#include <stddef.h>

static inline size_t plb_backing_page_index(size_t first, size_t count, size_t step, int reverse) {
    return first + (reverse ? count - 1 - step : step);
}
#endif
