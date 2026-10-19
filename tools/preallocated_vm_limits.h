/* MIT. Exact diskless working sets, under the user's 6 GiB combined budget. */
#ifndef PLB_PREALLOCATED_VM_LIMITS_H
#define PLB_PREALLOCATED_VM_LIMITS_H
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const int plb_preallocated_pairs[][2] = {{512, 256},   {1024, 768},  {1536, 1024},
                                                {2048, 1536}, {3072, 2048}, {4096, 3072},
                                                {4480, 4096}, {5120, 4096}, {5888, 4096}};

static inline int plb_preallocated_workload(int memory, int touch) {
    for (size_t i = 0; i < sizeof(plb_preallocated_pairs) / sizeof(plb_preallocated_pairs[0]);
         i++) {
        if (memory == plb_preallocated_pairs[i][0] && touch == plb_preallocated_pairs[i][1]) {
            return memory + 256 <= 6144;
        }
    }
    return 0;
}

static inline int plb_preallocated_workload_mode(int memory, int touch, int pin) {
    return plb_preallocated_workload(memory, touch) && memory + 256 + (pin ? 768 : 0) <= 6144;
}

static inline int plb_preallocated_policy_mode(const char *option, int *memory, int *touch,
                                               int chunked) {
    if (chunked < 0 || chunked > 4) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(plb_preallocated_pairs) / sizeof(plb_preallocated_pairs[0]);
         i++) {
        char expected[64];
        const char *prefix = chunked == 4   ? "normal-first-collapse-"
                             : chunked == 3 ? "normal-first-pin-"
                             : chunked == 2 ? "normal-first-"
                             : chunked      ? "chunked-"
                                            : "";
        snprintf(expected, sizeof(expected), "--preallocated-%s%d-%d", prefix,
                 plb_preallocated_pairs[i][0], plb_preallocated_pairs[i][1]);
        if (!strcmp(option, expected)) {
            if (!plb_preallocated_workload_mode(plb_preallocated_pairs[i][0],
                                                plb_preallocated_pairs[i][1], chunked == 3)) {
                return 0;
            }
            *memory = plb_preallocated_pairs[i][0];
            *touch = plb_preallocated_pairs[i][1];
            return 1;
        }
    }
    return 0;
}

static inline size_t plb_preallocated_offset(size_t ordinal, size_t bytes, size_t batch,
                                             int normal_first) {
    if (!batch || bytes % batch || ordinal >= bytes / batch) {
        return (size_t)-1;
    }
    size_t at = ordinal * batch;
    size_t boundary = 2048UL * 1024 * 1024;
    if (normal_first && bytes > boundary) {
        if (at < bytes - boundary) {
            return boundary + at;
        }
        return at - (bytes - boundary);
    }
    return at;
}

static inline int plb_preallocated_policy(const char *option, int *memory, int *touch) {
    return plb_preallocated_policy_mode(option, memory, touch, 0);
}
#endif
