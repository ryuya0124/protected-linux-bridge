/* MIT. Fixed developer-diagnostic RAM budgets, including the resident target.
 * Keep execution overhead headroom and reject removed/unknown pressure modes. */
#ifndef PLB_ALLOCATION_PROBE_LIMITS_H
#define PLB_ALLOCATION_PROBE_LIMITS_H
#include <stddef.h>
#include <string.h>

#define PLB_PROBE_MIB (1024UL * 1024)
#define PLB_PROBE_RAM_CAP (6144UL * PLB_PROBE_MIB)
#define PLB_PROBE_TARGET_BYTES (16UL * PLB_PROBE_MIB)
#define PLB_PROBE_OVERHEAD_RESERVE (16UL * PLB_PROBE_MIB)
#define PLB_PROBE_AVAILABLE_FLOOR_KIB (1536UL * 1024)

static size_t plb_pressure_limit(const char *mode) {
    if (!mode)
        return 0;
    size_t total;
    if (!strcmp(mode, "reclaim-4g"))
        total = 4096UL * PLB_PROBE_MIB;
    else if (!strcmp(mode, "reclaim-6g"))
        total = PLB_PROBE_RAM_CAP;
    else
        return 0;
    return total - PLB_PROBE_TARGET_BYTES - PLB_PROBE_OVERHEAD_RESERVE;
}

static size_t plb_pressure_chunk(size_t touched, size_t cap) {
    const size_t batch = 64UL * PLB_PROBE_MIB;
    if (!cap || cap > PLB_PROBE_RAM_CAP || touched >= cap)
        return 0;
    return cap - touched < batch ? cap - touched : batch;
}
#endif
