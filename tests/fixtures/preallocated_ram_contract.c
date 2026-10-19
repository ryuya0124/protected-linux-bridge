#include <assert.h>
#include <string.h>
#include "preallocated_ram_contract.h"

int main(void) {
    struct stat actual = {.st_mode = S_IFREG | 0600,
                          .st_size = PLB_HANDOFF_BYTES,
                          .st_dev = 5,
                          .st_ino = 91,
                          .st_nlink = 0};
    assert(plb_handoff_contract(&actual, O_RDWR, 7, 5, 91));
    /* No-execute seal may coexist with the required crosvm sealing contract. */
    assert(plb_handoff_contract(&actual, O_RDWR, 7 | 32, 5, 91));
    assert(!plb_handoff_contract(&actual, O_RDONLY, 7, 5, 91));
    assert(!plb_handoff_contract(&actual, O_RDWR, 7, 5, 92));
    assert(!plb_handoff_contract(&actual, O_RDWR, 1, 5, 91));
    assert(!plb_handoff_contract(&actual, O_RDWR, 7 | 8, 5, 91));
    assert(!plb_handoff_contract(&actual, O_RDWR, 7 | 16, 5, 91));
    assert(!plb_handoff_contract(&actual, O_RDWR, -1, 5, 91));
    actual.st_size--;
    assert(!plb_handoff_contract(&actual, O_RDWR, 7, 5, 91));
    actual.st_size++;
    actual.st_nlink = 1;
    assert(!plb_handoff_contract(&actual, O_RDWR, 7, 5, 91));
    return 0;
}
