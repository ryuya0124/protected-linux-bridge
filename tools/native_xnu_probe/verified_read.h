/* MIT. Private-snapshot read verification for an explicitly pinned guest disk.
 * The caller serializes this state and supplies an immutable trusted hash table.
 * Only verified private bytes may be copied into the returned page.
 */
#ifndef PLB_VERIFIED_READ_H
#define PLB_VERIFIED_READ_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PLB_VERIFIED_CHUNK_BYTES 16384U
#define PLB_VERIFIED_PAGE_BYTES 4096U
struct plb_verified_state {
    uint8_t snapshot[PLB_VERIFIED_CHUNK_BYTES];
    uint64_t chunk;
    uint64_t verified_chunks;
    uint64_t delivered_pages;
    uint64_t failures;
    bool valid;
    bool poisoned;
};
typedef bool (*plb_verified_raw_read)(void *, uint64_t, uint8_t[4096]);
typedef bool (*plb_verified_hash)(uint64_t, const uint8_t *, uint32_t, uint8_t[32]);

static bool plb_verified_read(struct plb_verified_state *state, uint64_t image_bytes,
                              const uint8_t *hashes, size_t hash_bytes, uint64_t offset,
                              uint8_t destination[4096], plb_verified_raw_read read_raw,
                              void *context, plb_verified_hash hash_snapshot) {
    if (destination) {
        memset(destination, 0, PLB_VERIFIED_PAGE_BYTES);
    }
    if (!state || !destination || !hashes || !read_raw || !hash_snapshot || !image_bytes ||
        (image_bytes & 511) || offset >= image_bytes || (offset & 4095) || state->poisoned) {
        return false;
    }
    uint64_t count = (image_bytes - 1) / PLB_VERIFIED_CHUNK_BYTES + 1;
    if (count > SIZE_MAX / 32 || hash_bytes != count * 32) {
        return false;
    }
    uint64_t chunk = offset / PLB_VERIFIED_CHUNK_BYTES;
    uint64_t begin = chunk * PLB_VERIFIED_CHUNK_BYTES;
    uint32_t span = image_bytes - begin < PLB_VERIFIED_CHUNK_BYTES ? (uint32_t)(image_bytes - begin)
                                                                   : PLB_VERIFIED_CHUNK_BYTES;
    uint32_t bytes = image_bytes - offset < PLB_VERIFIED_PAGE_BYTES
                         ? (uint32_t)(image_bytes - offset)
                         : PLB_VERIFIED_PAGE_BYTES;
    if (!state->valid || state->chunk != chunk) {
        state->valid = false;
        memset(state->snapshot, 0, sizeof(state->snapshot));
        for (uint32_t at = 0; at < span; at += PLB_VERIFIED_PAGE_BYTES) {
            if (!read_raw(context, begin + at, state->snapshot + at)) {
                goto fail;
            }
        }
        uint8_t digest[32];
        if (!hash_snapshot(chunk, state->snapshot, span, digest)) {
            goto fail;
        }
        uint8_t difference = 0;
        for (unsigned i = 0; i < 32; i++) {
            difference |= digest[i] ^ hashes[chunk * 32 + i];
        }
        if (difference) {
            goto fail;
        }
        state->chunk = chunk;
        state->valid = true;
        state->verified_chunks++;
    }
    memcpy(destination, state->snapshot + (size_t)(offset - begin), bytes);
    state->delivered_pages++;
    return true;
fail:
    state->valid = false;
    state->poisoned = true;
    state->failures++;
    memset(state->snapshot, 0, sizeof(state->snapshot));
    return false;
}
#endif
