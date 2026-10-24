/* MIT. Bounded provider identity and mounted-root audit predicates.
 * These checks describe a distinct owner input policy, never Apple signing.
 */
#ifndef PLB_ROOT_BINDING_H
#define PLB_ROOT_BINDING_H
#include <stdbool.h>
#include <stddef.h>

#define PLB_ROOT_PROVIDER_LIMIT 16U
typedef void *(*plb_root_parent)(void *, void *);

static unsigned plb_root_provider_depth(void *media, void *owner, plb_root_parent parent,
                                        void *context) {
    if (!media || !owner || !parent) {
        return 0;
    }
    for (unsigned depth = 1; media && depth <= PLB_ROOT_PROVIDER_LIMIT; depth++) {
        if (media == owner) {
            return depth;
        }
        media = parent(media, context);
    }
    return 0;
}

struct plb_root_binding_facts {
    bool apfs;
    bool readonly;
    bool device_valid;
    unsigned media_matches;
    bool uuid_matches;
    unsigned provider_depth;
    bool integrity_healthy;
};

static unsigned plb_root_binding_mask(const struct plb_root_binding_facts *facts) {
    if (!facts) {
        return 0;
    }
    return (facts->apfs ? 1U : 0U) | (facts->readonly ? 2U : 0U) | (facts->device_valid ? 4U : 0U) |
           (facts->media_matches == 1 ? 8U : 0U) | (facts->uuid_matches ? 16U : 0U) |
           (facts->provider_depth && facts->provider_depth <= PLB_ROOT_PROVIDER_LIMIT ? 32U : 0U) |
           (facts->integrity_healthy ? 64U : 0U);
}

#define PLB_ROOT_BINDING_REQUIRED 127U
static bool plb_root_binding_qualified(const struct plb_root_binding_facts *facts) {
    return plb_root_binding_mask(facts) == PLB_ROOT_BINDING_REQUIRED;
}
#endif
