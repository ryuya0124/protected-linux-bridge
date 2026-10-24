/* MIT. Include after the platform's real SHA256_CTX/Init/Update/Final API.
 * This same domain-separated hash implementation is exercised on the host.
 */
#ifndef PLB_VERIFIED_HASH_H
#define PLB_VERIFIED_HASH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void plb_verified_sha256(const void *data, size_t bytes, uint8_t digest[32]) {
    SHA256_CTX context;
    SHA256_Init(&context);
    SHA256_Update(&context, data, bytes);
    SHA256_Final(digest, &context);
}

static bool plb_verified_hash_snapshot(uint64_t index, const uint8_t *data, uint32_t bytes,
                                       uint8_t digest[32]) {
    static const uint8_t domain[] = "PLB-IOS-BLOCK-v1";
    uint8_t identity[12];
    for (unsigned i = 0; i < 8; i++) {
        identity[i] = (uint8_t)(index >> (8 * i));
    }
    for (unsigned i = 0; i < 4; i++) {
        identity[8 + i] = (uint8_t)(bytes >> (8 * i));
    }
    SHA256_CTX context;
    SHA256_Init(&context);
    SHA256_Update(&context, domain, sizeof(domain));
    SHA256_Update(&context, identity, sizeof(identity));
    SHA256_Update(&context, data, bytes);
    SHA256_Final(digest, &context);
    return true;
}
#endif
