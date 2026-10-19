#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include "preallocated_collapse.h"

int main(void) {
    char path[] = "/tmp/plb-alignment-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    unlink(path);
    const size_t bytes = PLB_COLLAPSE_BYTES;
    assert(!ftruncate(fd, bytes * 3));
    assert(plb_aligned_shared_mapping(fd, bytes - 4096, 0) == MAP_FAILED);
    assert(plb_aligned_shared_mapping(fd, bytes, 4096) == MAP_FAILED);
    assert(plb_aligned_shared_mapping(fd, bytes, -1) == MAP_FAILED);
    unsigned char *mapping = plb_aligned_shared_mapping(fd, bytes, bytes);
    assert(mapping != MAP_FAILED && (uintptr_t)mapping % bytes == 0);
    mapping[0] = 0x71;
    mapping[bytes - 1] = 0x29;
    unsigned char value;
    assert(pread(fd, &value, 1, bytes) == 1 && value == 0x71);
    assert(pread(fd, &value, 1, 2 * bytes - 1) == 1 && value == 0x29);
    assert(pread(fd, &value, 1, 0) == 1 && value == 0);
    assert(pread(fd, &value, 1, 2 * bytes) == 1 && value == 0);
    assert(!munmap(mapping, bytes));
    close(fd);
    return 0;
}
