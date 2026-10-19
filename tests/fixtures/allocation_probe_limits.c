/* MIT. Check total budgets and partial final batches without allocating RAM. */
#include "allocation_probe_limits.h"
#include <assert.h>

int main(void) {
    const char *accepted[] = {"reclaim-4g", "reclaim-6g"};
    for (size_t i = 0; i < sizeof(accepted) / sizeof(*accepted); i++) {
        size_t cap = plb_pressure_limit(accepted[i]);
        assert(cap > 0);
        assert(cap + PLB_PROBE_TARGET_BYTES + PLB_PROBE_OVERHEAD_RESERVE <= PLB_PROBE_RAM_CAP);
        size_t touched = 0, last = 0;
        while (touched < cap) {
            size_t chunk = plb_pressure_chunk(touched, cap);
            assert(chunk && chunk % 4096 == 0 && chunk <= 64UL * PLB_PROBE_MIB);
            assert(chunk <= cap - touched);
            touched += chunk;
            last = chunk;
        }
        assert(touched == cap && last < 64UL * PLB_PROBE_MIB);
        assert(plb_pressure_chunk(touched, cap) == 0);
    }
    assert(plb_pressure_limit("reclaim-8g") == 0);
    assert(plb_pressure_limit("reclaim-7g") == 0);
    assert(plb_pressure_limit("reclaim-0g") == 0);
    assert(plb_pressure_limit("") == 0);
    assert(plb_pressure_limit(NULL) == 0);
    assert(plb_pressure_chunk(0, PLB_PROBE_RAM_CAP + 4096) == 0);
    assert(PLB_PROBE_AVAILABLE_FLOOR_KIB == 1536UL * 1024);
    return 0;
}
