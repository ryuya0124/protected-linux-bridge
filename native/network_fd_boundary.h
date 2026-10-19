/* MIT. The unprivileged relay must not inherit VM RAM, disks or control FDs. */
#ifndef PLB_NETWORK_FD_BOUNDARY_H
#define PLB_NETWORK_FD_BOUNDARY_H
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

static int plb_network_close_inherited(int listener) {
    DIR *directory = opendir("/proc/self/fd");
    if (!directory)
        return -1;
    int scan_fd = dirfd(directory), result = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.')
            continue;
        char *end;
        long value = strtol(entry->d_name, &end, 10);
        if (*end || value < 0 || value > INT_MAX) {
            result = -1;
            break;
        }
        int fd = (int)value;
        if (fd > STDERR_FILENO && fd != listener && fd != scan_fd)
            close(fd);
        errno = 0;
    }
    if (errno)
        result = -1;
    closedir(directory);
    return result;
}
#endif
