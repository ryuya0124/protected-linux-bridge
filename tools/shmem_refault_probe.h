/* MIT. Diagnostic-only refaulting of an owned, not-yet-donated shared memfd.
 * Never use on an active VM, a persistent file, or a concurrently modified map.
 * Hole punching is bounded to this mapping and preserves every original byte.
 */
#ifndef PLB_SHMEM_REFAULT_PROBE_H
#define PLB_SHMEM_REFAULT_PROBE_H
#include <sys/syscall.h>
#include <linux/falloc.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "owned_page_summary.h"

/* A 32 KiB subrange may also lie inside a larger folio. Contiguous PFNs
 * alone, or two adjacent 16 KiB folios, do not satisfy this observation. */
static int plb_refault_large_window(int pagemap, int flags_fd, const void *address) {
    uint64_t entries[8], first = 0;
    off_t index = (off_t)((uintptr_t)address / 4096 * 8);
    if (pread(pagemap, entries, sizeof(entries), index) != sizeof(entries))
        return -1;
    for (size_t i = 0; i < 8; i++) {
        uint64_t pfn = entries[i] & ((1ULL << 55) - 1), flags;
        if (!(entries[i] & (1ULL << 63)) || !pfn ||
            pread(flags_fd, &flags, 8, (off_t)(pfn * 8)) != 8)
            return -1;
        if (!i)
            first = pfn;
        if (!(flags & PLB_KPF_THP) || pfn != first + i || first % 8 ||
            (i && ((flags & PLB_KPF_COMPOUND_HEAD) || !(flags & PLB_KPF_COMPOUND_TAIL))))
            return 0;
    }
    return 1;
}

static int plb_refault_owned_shmem(int fd, void *mapping, size_t bytes, off_t file_offset,
                                   int rounds) {
    const size_t block = 32768;
    if (fd < 0 || (uintptr_t)mapping % block || bytes % block || file_offset % block ||
        rounds < 1 || rounds > 4)
        return -1;
    int pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    int flags = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    if (pagemap < 0 || flags < 0) {
        if (pagemap >= 0)
            close(pagemap);
        if (flags >= 0)
            close(flags);
        return -1;
    }
    int failed = 0;
    unsigned char saved[32768];
    for (int round = 1; round <= rounds && !failed; round++) {
        size_t kept = 0, retried = 0;
        for (size_t at = 0; at < bytes && !failed; at += block) {
            unsigned char *target = (unsigned char *)mapping + at;
            /* Read all bytes before inspecting residency or punching a hole.
             * Even an observation failure leaves the original bytes intact. */
            memcpy(saved, target, block);
            int large = plb_refault_large_window(pagemap, flags, target);
            if (large < 0) {
                failed = 1;
            } else if (large) {
                kept++;
            } else if (syscall(SYS_fallocate, fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                               file_offset + (off_t)at, (off_t)block)) {
                failed = 1;
            } else {
                memcpy(target, saved, block);
                for (size_t byte = 0; byte < block; byte++)
                    if (((volatile unsigned char *)target)[byte] != saved[byte])
                        failed = 1;
                retried++;
            }
        }
        dprintf(STDERR_FILENO,
                "PLB_SHMEM_REFAULT round=%d block_bytes=%zu kept_large_windows=%zu "
                "retried=%zu errors=%d\n",
                round, block, kept, retried, failed);
    }
    close(pagemap);
    close(flags);
    return failed ? -1 : 0;
}
#endif
