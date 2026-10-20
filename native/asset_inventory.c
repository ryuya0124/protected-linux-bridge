/* MIT. Read-only accounting for fixed app-private VM assets.
 * Never follows symlinks, crosses filesystems, starts a VM or removes a file.
 * Directory totals are a bounded live estimate, not an atomic disk snapshot. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#ifndef PLB_ASSET_ROOT
#define PLB_ASSET_ROOT "/data/user/0/net.ryuya_dev.protectedlinux/files"
#endif

struct totals {
    uint64_t logical, allocated, files, entries;
    int partial;
};

static void walk(int fd, dev_t device, unsigned depth, struct totals *total) {
    DIR *directory = fdopendir(dup(fd));
    if (!directory) {
        total->partial = 1;
        return;
    }
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (++total->entries > 100000) {
            total->partial = 1;
            break;
        }
        struct stat info;
        if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) || info.st_dev != device) {
            total->partial = 1;
            continue;
        }
        if (S_ISREG(info.st_mode)) {
            total->logical += (uint64_t)info.st_size;
            total->allocated += (uint64_t)info.st_blocks * 512;
            total->files++;
        } else if (S_ISDIR(info.st_mode) && depth < 12) {
            int child = openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            struct stat opened;
            if (child < 0 || fstat(child, &opened) || opened.st_dev != info.st_dev ||
                opened.st_ino != info.st_ino) {
                total->partial = 1;
            } else {
                walk(child, device, depth + 1, total);
            }
            if (child >= 0)
                close(child);
        } else {
            total->partial = 1;
        }
        errno = 0;
    }
    if (errno)
        total->partial = 1;
    closedir(directory);
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1)
        return 2;
    int root = open(PLB_ASSET_ROOT, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat root_info;
    struct statvfs space;
    if (root < 0 || fstat(root, &root_info) || fstatvfs(root, &space)) {
        if (root >= 0)
            close(root);
        return 3;
    }
    int lock = openat(root, "vm-owner.lock", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    int busy = -1;
    if (lock >= 0) {
        struct stat info;
        if (!fstat(lock, &info) && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
            info.st_nlink == 1 && !(info.st_mode & 077)) {
            if (!flock(lock, LOCK_EX | LOCK_NB)) {
                busy = 0;
                flock(lock, LOCK_UN);
            } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
                busy = 1;
            }
        }
        close(lock);
    }
    printf("{\"schema\":1,\"available\":%llu,\"total\":%llu,\"busy\":%d,\"items\":[",
           (unsigned long long)space.f_bavail * space.f_frsize,
           (unsigned long long)space.f_blocks * space.f_frsize, busy);
    const char *names[] = {"runtime",
                           "android-gpu4g-runtime",
                           "android-gpu4g-surface-runtime",
                           "kernels",
                           "vm",
                           "vm-mainline",
                           "android-guests",
                           "kernel-probes",
                           "debian-images",
                           "backup-recovery",
                           "backup-jobs"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        struct totals total = {0};
        int present = 0;
        int fd = openat(root, names[index], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd >= 0) {
            struct stat info;
            present = 1;
            if (fstat(fd, &info) || info.st_dev != root_info.st_dev)
                total.partial = 1;
            else
                walk(fd, info.st_dev, 0, &total);
            close(fd);
        } else if (errno != ENOENT) {
            total.partial = 1;
        }
        printf("%s{\"id\":\"%s\",\"present\":%s,\"logical\":%llu,\"allocated\":%llu,"
               "\"files\":%llu,\"partial\":%s}",
               index ? "," : "", names[index], present ? "true" : "false",
               (unsigned long long)total.logical, (unsigned long long)total.allocated,
               (unsigned long long)total.files, total.partial ? "true" : "false");
    }
    puts("]}");
    close(root);
    return ferror(stdout) ? 4 : 0;
}
