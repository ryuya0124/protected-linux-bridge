/* MIT. Retired helper must never punch, unmap or alter the supplied mapping. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include "shmem_refault_probe.h"

int main(void) {
    char path[] = "/tmp/plb-retired-refault-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && unlink(path) == 0);
    size_t bytes = 65536;
    assert(ftruncate(fd, (off_t)bytes) == 0);
    unsigned char *data = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(data != MAP_FAILED);
    for (size_t i = 0; i < bytes; i++)
        data[i] = (unsigned char)(i % 251 + 1);
    errno = 0;
    assert(plb_refault_owned_shmem(fd, data, bytes, 0, 4) == -1 && errno == ENOTSUP);
    for (size_t i = 0; i < bytes; i++)
        assert(data[i] == (unsigned char)(i % 251 + 1));
    assert(lseek(fd, 0, SEEK_END) == (off_t)bytes);
    assert(munmap(data, bytes) == 0);
    close(fd);
    return 0;
}
