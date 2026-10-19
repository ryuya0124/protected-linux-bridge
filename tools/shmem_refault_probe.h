/* MIT. Retired mapped-hole-punch experiment: refuse before modifying a memfd.
 * A large reverse-prefault trial produced host "still mapped when deleted"
 * diagnostics inside filemap_unaccount_folio. Preserve the old implementation
 * only in Git history (v0.4.0-preview.5), not as an executable fallback.
 * The app now uses prefault-only admission, with no hole punching or refaulting.
 */
#ifndef PLB_SHMEM_REFAULT_PROBE_H
#define PLB_SHMEM_REFAULT_PROBE_H
#include <sys/types.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static int plb_refault_owned_shmem(int fd, void *mapping, size_t bytes, off_t file_offset,
                                   int rounds) {
    (void)fd;
    (void)mapping;
    (void)bytes;
    (void)file_offset;
    (void)rounds;
    dprintf(STDERR_FILENO, "PLB_SHMEM_REFAULT_DISABLED host_page_cache_warning=1\n");
    errno = ENOTSUP;
    return -1;
}
#endif
