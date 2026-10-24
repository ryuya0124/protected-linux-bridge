/* MIT. Exercise the real private snapshot verifier with real system SHA256. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#define SHA256_CTX CC_SHA256_CTX
#define SHA256_Init CC_SHA256_Init
#define SHA256_Update CC_SHA256_Update
#define SHA256_Final(digest, context) CC_SHA256_Final(digest, context)
#else
#include <openssl/sha.h>
#endif
#include "../../tools/native_xnu_probe/verified_hash.h"
#include "../../tools/native_xnu_probe/verified_read.h"

#define IMAGE_BYTES (2 * PLB_VERIFIED_CHUNK_BYTES + 1536)
struct backend {
    uint8_t data[IMAGE_BYTES];
    unsigned calls;
    unsigned fail_call;
    bool corrupt;
};
static bool read_raw(void *opaque, uint64_t offset, uint8_t destination[4096]) {
    struct backend *backend = opaque;
    backend->calls++;
    if (backend->fail_call == backend->calls) {
        return false;
    }
    assert(offset < IMAGE_BYTES && !(offset & 4095));
    size_t bytes = IMAGE_BYTES - offset < 4096 ? IMAGE_BYTES - offset : 4096;
    memset(destination, 0xa5, 4096); // Malicious/untrusted padding beyond EOF.
    memcpy(destination, backend->data + offset, bytes);
    if (backend->corrupt) {
        destination[0] ^= 1;
    }
    return true;
}
static bool failed_hash(uint64_t index, const uint8_t *data, uint32_t size, uint8_t digest[32]) {
    (void)index;
    (void)data;
    (void)size;
    (void)digest;
    return false;
}
static bool zero_page(const uint8_t page[4096]) {
    for (unsigned i = 0; i < 4096; i++) {
        if (page[i]) {
            return false;
        }
    }
    return true;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    uint8_t hashes[96], page[4096], digest[32];
    FILE *file = fopen(argv[1], "rb");
    assert(file && fread(hashes, 1, sizeof(hashes), file) == sizeof(hashes));
    assert(fgetc(file) == EOF && fclose(file) == 0);
    struct backend backend = {0};
    for (unsigned i = 0; i < IMAGE_BYTES; i++) {
        backend.data[i] = (uint8_t)(i * 17 + (i >> 9) * 29);
    }
    plb_verified_sha256("abc", 3, digest);
    assert(digest[0] == 0xba && digest[31] == 0xad);
    struct plb_verified_state state = {0};
#define READ(offset)                                                                               \
    plb_verified_read(&state, IMAGE_BYTES, hashes, sizeof(hashes), offset, page, read_raw,         \
                      &backend, plb_verified_hash_snapshot)
    assert(READ(0) && backend.calls == 4 && state.verified_chunks == 1);
    assert(memcmp(page, backend.data, 4096) == 0);
    backend.data[0] ^= 1;
    assert(READ(0) && backend.calls == 4 && page[0] == 0); // Own verified cache, not mutable DMA.
    backend.data[0] ^= 1;
    assert(READ(4096) && backend.calls == 4 && state.delivered_pages == 3);
    assert(READ(2 * PLB_VERIFIED_CHUNK_BYTES) && backend.calls == 5);
    assert(memcmp(page, backend.data + 2 * PLB_VERIFIED_CHUNK_BYTES, 1536) == 0);
    for (unsigned i = 1536; i < 4096; i++) {
        assert(page[i] == 0);
    }
    // A forged chunk never publishes bytes; even previously valid data stays refused afterward.
    backend.corrupt = true;
    assert(!READ(PLB_VERIFIED_CHUNK_BYTES) && zero_page(page));
    assert(state.poisoned && !state.valid && state.failures == 1 && state.delivered_pages == 4);
    unsigned calls = backend.calls;
    assert(!READ(0) && backend.calls == calls && zero_page(page));
    for (unsigned failure = 1; failure <= 4; failure++) {
        memset(&state, 0, sizeof(state));
        backend.calls = 0;
        backend.fail_call = failure;
        backend.corrupt = false;
        assert(!READ(0) && state.poisoned && !state.delivered_pages && zero_page(page));
    }
    backend.fail_call = 0;
    memset(&state, 0, sizeof(state));
    uint8_t temporary[32];
    memcpy(temporary, hashes, 32);
    memcpy(hashes, hashes + 32, 32);
    memcpy(hashes + 32, temporary, 32);
    assert(!READ(0) && state.poisoned && zero_page(page)); // Index/table substitution.
    memset(&state, 0, sizeof(state));
    assert(!READ(UINT64_MAX) && zero_page(page));
    assert(!READ(512) && zero_page(page));
    assert(!READ(IMAGE_BYTES) && zero_page(page));
    assert(!plb_verified_read(&state, IMAGE_BYTES, hashes, sizeof(hashes) - 1, 0, page, read_raw,
                              &backend, plb_verified_hash_snapshot));
    assert(!plb_verified_read(&state, IMAGE_BYTES, hashes, sizeof(hashes), 0, page, read_raw,
                              &backend, failed_hash));
    assert(state.poisoned && zero_page(page));
    puts("verified private snapshot controls passed");
    return 0;
}
