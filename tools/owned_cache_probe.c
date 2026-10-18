/* MIT. Release only cold cache of this app's new Android immutable image files.
 * No global cache-drop/compaction request and no content or metadata writes.
 * Existing Debian files, the installed host kernel, and other apps are excluded.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#define PLB_OWNER_LOCK_FILE FILES "/vm-owner.lock"
#include "../native/profile_lock.h"

static size_t resident(int fd, size_t bytes) {
    size_t pages = (bytes + 4095) / 4096;
    unsigned char *state = malloc(pages);
    void *map = mmap(NULL, bytes, PROT_READ, MAP_SHARED, fd, 0);
    if (!state || map == MAP_FAILED) {
        if (map != MAP_FAILED)
            munmap(map, bytes);
        free(state);
        return (size_t)-1;
    }
    size_t count = 0;
    if (mincore(map, bytes, state))
        count = (size_t)-1;
    else
        for (size_t i = 0; i < pages; i++)
            count += state[i] & 1;
    munmap(map, bytes);
    free(state);
    return count;
}

int main(int argc, char **argv) {
    (void)argv;
    if (getuid() || argc != 1 || sysconf(_SC_PAGESIZE) != 4096)
        return 20;
    int lock = profile_lock();
    if (lock < 0)
        return 28;
    const char *paths[] = {
        FILES "/android-guests/cuttlefish17/android-base.qcow2",
        FILES "/android-guests/cuttlefish17/android-ramdisk.cpio",
        FILES "/android-guests/cuttlefish17/android-lowram.cpio",
        FILES "/kernels/android17-cuttlefish/Image",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        struct stat info;
        if (lstat(paths[i], &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink != 1 ||
            info.st_mode & (0222 | 077) || info.st_size <= 0 ||
            (unsigned long long)info.st_size > 2ULL * 1024 * 1024 * 1024)
            return 21;
        int fd = open(paths[i], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0)
            return 22;
        struct stat opened;
        if (fstat(fd, &opened) || opened.st_dev != info.st_dev || opened.st_ino != info.st_ino ||
            opened.st_size != info.st_size || opened.st_uid || opened.st_nlink != 1 ||
            !S_ISREG(opened.st_mode) || opened.st_mode & (0222 | 077)) {
            close(fd);
            return 23;
        }
        size_t before = resident(fd, (size_t)info.st_size);
        if (before == (size_t)-1) {
            close(fd);
            return 24;
        }
        int result = posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        size_t after = resident(fd, (size_t)info.st_size);
        printf(
            "PLB_OWNED_CACHE index=%zu bytes=%lld resident_pages_before=%zu after=%zu result=%d\n",
            i, (long long)info.st_size, before, after, result);
        close(fd);
        if (result || after == (size_t)-1)
            return 25;
    }
    close(lock);
    return 0;
}
