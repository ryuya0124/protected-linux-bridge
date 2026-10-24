/* MIT. Fail-closed root audit: duplicate identity, unverified state and cycles. */
#include <assert.h>
#include <stdio.h>
#include "../../tools/native_xnu_probe/root_binding.h"

struct node {
    struct node *provider;
};
static void *parent(void *opaque, void *context) {
    unsigned *calls = context;
    (*calls)++;
    return ((struct node *)opaque)->provider;
}

int main(void) {
    struct node nodes[18] = {0};
    for (unsigned i = 0; i < 17; i++) {
        nodes[i].provider = &nodes[i + 1];
    }
    unsigned calls = 0;
    assert(plb_root_provider_depth(&nodes[0], &nodes[8], parent, &calls) == 9);
    assert(calls == 8);
    calls = 0;
    assert(plb_root_provider_depth(&nodes[0], &nodes[15], parent, &calls) == 16);
    calls = 0;
    assert(plb_root_provider_depth(&nodes[0], &nodes[16], parent, &calls) == 0);
    assert(calls == 16);
    nodes[4].provider = &nodes[2]; // A cycle cannot spin indefinitely or reach an alien owner.
    calls = 0;
    assert(!plb_root_provider_depth(&nodes[0], &nodes[8], parent, &calls) && calls == 16);
    assert(!plb_root_provider_depth(NULL, &nodes[8], parent, &calls));
    assert(!plb_root_provider_depth(&nodes[0], NULL, parent, &calls));
    assert(!plb_root_provider_depth(&nodes[0], &nodes[8], NULL, &calls));
    struct plb_root_binding_facts good = {true, true, true, 1, true, 9, true};
    assert(plb_root_binding_qualified(&good));
    assert(plb_root_binding_mask(&good) == PLB_ROOT_BINDING_REQUIRED);
    assert(!plb_root_binding_qualified(NULL));
    // Each independently missing security fact must refuse, including a duplicate
    // dev_t match and a verified provider which has since become unavailable.
    for (unsigned condition = 0; condition < 9; condition++) {
        struct plb_root_binding_facts bad = good;
        switch (condition) {
        case 0:
            bad.apfs = false;
            break;
        case 1:
            bad.readonly = false;
            break;
        case 2:
            bad.device_valid = false;
            break;
        case 3:
            bad.media_matches = 0;
            break;
        case 4:
            bad.media_matches = 2;
            break;
        case 5:
            bad.uuid_matches = false;
            break;
        case 6:
            bad.provider_depth = 0;
            break;
        case 7:
            bad.provider_depth = PLB_ROOT_PROVIDER_LIMIT + 1;
            break;
        case 8:
            bad.integrity_healthy = false;
            break;
        }
        assert(!plb_root_binding_qualified(&bad));
    }
    puts("bounded mounted-root audit controls passed");
    return 0;
}
