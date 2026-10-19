/* MIT. Process-local PMD collapse on a fresh owned memfd, never file holes.
 * MAP_FIXED may only replace the interior of a new PROT_NONE reservation.
 * Failure is reported, not converted into a claim of a huge-page allocation.
 */
#ifndef PLB_PREALLOCATED_COLLAPSE_H
#define PLB_PREALLOCATED_COLLAPSE_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>

#define PLB_COLLAPSE_BYTES (2UL * 1024 * 1024)
#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif

static inline void *plb_aligned_shared_mapping(int fd, size_t bytes, off_t offset) {
    const size_t alignment = PLB_COLLAPSE_BYTES;
    if (!bytes || bytes % alignment || offset < 0 || (uint64_t)offset % alignment ||
        bytes > SIZE_MAX - alignment) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    void *reservation =
        mmap(NULL, bytes + alignment, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reservation == MAP_FAILED) {
        return MAP_FAILED;
    }
    uintptr_t begin = (uintptr_t)reservation;
    uintptr_t aligned = (begin + alignment - 1) & ~(alignment - 1);
    void *mapping =
        mmap((void *)aligned, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, offset);
    if (mapping == MAP_FAILED) {
        int saved = errno;
        munmap(reservation, bytes + alignment);
        errno = saved;
        return MAP_FAILED;
    }
    if (aligned > begin) {
        munmap(reservation, aligned - begin);
    }
    size_t tail = begin + bytes + alignment - (aligned + bytes);
    if (tail) {
        munmap((void *)(aligned + bytes), tail);
    }
    return mapping;
}

static inline int plb_collapse_owned(void *mapping, size_t bytes) {
    if ((uintptr_t)mapping % PLB_COLLAPSE_BYTES || !bytes || bytes % PLB_COLLAPSE_BYTES) {
        return -1;
    }
    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    errno = 0;
    int result = madvise(mapping, bytes, MADV_COLLAPSE);
    int saved = errno;
    clock_gettime(CLOCK_MONOTONIC, &after);
    long long micros =
        (after.tv_sec - before.tv_sec) * 1000000LL + (after.tv_nsec - before.tv_nsec) / 1000;
    printf("PLB_PREALLOCATED_COLLAPSE bytes=%zu result=%d errno=%d elapsed_us=%lld\n", bytes,
           result, saved, micros);
    return result;
}
#endif
