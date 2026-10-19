/* MIT. Position-dependent patterns exercise every word and detect page aliases. */
#ifndef PLB_FULL_MEMORY_PATTERN_H
#define PLB_FULL_MEMORY_PATTERN_H
#include <stddef.h>
#include <stdint.h>

static inline uint64_t plb_full_word_pattern(size_t offset, size_t word, int round) {
    uint64_t value =
        UINT64_C(0x713bd4c9a208f65e) ^ (uint64_t)(offset + word * 8) ^ ((uint64_t)round << 56);
    value *= UINT64_C(0x9e3779b97f4a7c15);
    return value ^ (value >> 29);
}

static inline void plb_full_store(volatile uint64_t *words, size_t offset, int round) {
    for (size_t word = 0; word < 4096 / sizeof(*words); word++) {
        words[word] = plb_full_word_pattern(offset, word, round);
    }
}

static inline unsigned plb_full_check(volatile const uint64_t *words, size_t offset, int round) {
    unsigned errors = 0;
    for (size_t word = 0; word < 4096 / sizeof(*words); word++) {
        errors += words[word] != plb_full_word_pattern(offset, word, round);
    }
    return errors;
}
#endif
