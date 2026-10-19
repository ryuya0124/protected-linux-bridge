/* MIT. Reverse traversal must not touch the separately populated margins. */
#include "backing_page_order.h"
#include <assert.h>

int main(void) {
    const size_t first = 64 * 1024 / 4, tail = first;
    const size_t total = 3072 * 1024 / 4, count = total - first - tail;
    for (size_t step = 0; step < count; step++) {
        size_t forward = plb_backing_page_index(first, count, step, 0);
        size_t reverse = plb_backing_page_index(first, count, step, 1);
        assert(forward >= first && forward < total - tail);
        assert(reverse >= first && reverse < total - tail);
        assert(forward + reverse == 2 * first + count - 1);
    }
    assert(plb_backing_page_index(first, count, 0, 1) == total - tail - 1);
    assert(plb_backing_page_index(first, count, count - 1, 1) == first);
    assert(plb_backing_page_index(0, 1, 0, 1) == 0);
    return 0;
}
