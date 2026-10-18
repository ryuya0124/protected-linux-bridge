/* MIT. Small, process-local backing allocation experiment; no VM or device ioctl.
 * Allocates 16 MiB, optionally requests collapse of at most four owned 2 MiB
 * ranges, prints this process's accounting, then frees everything. No sysfs,
 * global compaction, THP policy, kernel, partition or protection setting writes.
 */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <linux/mount.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif

static void accounting(const char *stage) {
    FILE *input = fopen("/proc/self/smaps_rollup", "re");
    char line[256];
    printf("PLB_ALLOCATION_STAGE %s\n", stage);
    if (!input)
        return;
    while (fgets(line, sizeof(line), input))
        if (!strncmp(line, "Rss:", 4) || !strncmp(line, "AnonHugePages:", 14) ||
            !strncmp(line, "ShmemPmdMapped:", 15) || !strncmp(line, "Anonymous:", 10))
            fputs(line, stdout);
    fclose(input);
    fflush(stdout);
}

static void owned_page_flags(void *mapping, size_t bytes) {
    int pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    int flags = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    unsigned present = 0, hidden = 0, thp = 0, head = 0, tail = 0;
    if (pagemap < 0 || flags < 0)
        goto done;
    for (size_t at = 0; at < bytes; at += 4096) {
        uint64_t entry, page_flags;
        off_t index = (off_t)(((uintptr_t)mapping + at) / 4096 * 8);
        if (pread(pagemap, &entry, 8, index) != 8 || !(entry & (1ULL << 63)))
            continue;
        present++;
        uint64_t pfn = entry & ((1ULL << 55) - 1);
        if (!pfn) {
            hidden++;
            continue;
        }
        if (pread(flags, &page_flags, 8, (off_t)(pfn * 8)) != 8)
            continue;
        thp += !!(page_flags & (1ULL << 22));
        head += !!(page_flags & (1ULL << 15));
        tail += !!(page_flags & (1ULL << 16));
    }
done:
    if (pagemap >= 0)
        close(pagemap);
    if (flags >= 0)
        close(flags);
    /* Aggregate only. Never emit host physical/virtual addresses or PFNs. */
    printf("PLB_OWNED_PAGE_FLAGS present=%u hidden=%u thp=%u compound_head=%u compound_tail=%u\n",
           present, hidden, thp, head, tail);
}

static int detached_backing(void) {
    int config = (int)syscall(SYS_fsopen, "tmpfs", FSOPEN_CLOEXEC);
    if (config < 0)
        return -1;
    if (syscall(SYS_fsconfig, config, FSCONFIG_SET_STRING, "huge", "always", 0) ||
        syscall(SYS_fsconfig, config, FSCONFIG_SET_STRING, "size", "16m", 0) ||
        syscall(SYS_fsconfig, config, FSCONFIG_CMD_CREATE, NULL, NULL, 0)) {
        close(config);
        return -1;
    }
    /* Detached mount FD only. No attach, mountpoint, mount namespace change,
     * mount propagation, or alteration of the host's internal shmem mount. */
    int mountfd = (int)syscall(SYS_fsmount, config, FSMOUNT_CLOEXEC,
                               MOUNT_ATTR_NODEV | MOUNT_ATTR_NOEXEC | MOUNT_ATTR_NOSUID);
    close(config);
    if (mountfd < 0)
        return -1;
    int fd = openat(mountfd, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);
    close(mountfd);
    return fd;
}

int main(int argc, char **argv) {
    if (getuid() || sysconf(_SC_PAGESIZE) != 4096 || argc != 2 ||
        (strcmp(argv[1], "anon") && strcmp(argv[1], "shared") && strcmp(argv[1], "detached")))
        return 20;
    const size_t size = 16 * 1024 * 1024, huge = 2 * 1024 * 1024;
    int shared = strcmp(argv[1], "anon") != 0;
    int fd = -1;
    if (shared) {
        fd = !strcmp(argv[1], "detached")
                 ? detached_backing()
                 : (int)syscall(SYS_memfd_create, "plb-allocation-probe", 1);
        if (fd < 0 || ftruncate(fd, size)) {
            printf("PLB_ALLOCATION_BACKING_FAILED errno=%d\n", errno);
            return 21;
        }
    }
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     shared ? MAP_SHARED : MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
    if (map == MAP_FAILED)
        return 22;
    int advice = madvise(map, size, MADV_HUGEPAGE);
    printf("PLB_ALLOCATION_ADVICE mode=%s result=%d\n", argv[1], advice);
    volatile unsigned char *bytes = map;
    for (size_t at = 0; at < size; at += 4096)
        bytes[at] = 0x5a;
    accounting("populated");
    owned_page_flags(map, size);
    uintptr_t begin = ((uintptr_t)map + huge - 1) & ~(huge - 1);
    for (unsigned n = 0; n < 4; n++) {
        errno = 0;
        int result = madvise((void *)(begin + n * huge), huge, MADV_COLLAPSE);
        printf("PLB_ALLOCATION_COLLAPSE index=%u result=%d errno=%d\n", n, result, errno);
        fflush(stdout);
    }
    accounting("collapse-requested");
    owned_page_flags(map, size);
    unsigned errors = 0;
    for (size_t at = 0; at < size; at += 4096)
        if (bytes[at] != 0x5a)
            errors++;
    printf("PLB_ALLOCATION_READBACK errors=%u\n", errors);
    munmap(map, size);
    if (fd >= 0)
        close(fd);
    return errors ? 23 : 0;
}
