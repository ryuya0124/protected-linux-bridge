/* MIT. Observe only newly appended kernel records; no logging policy changes. */
#ifndef PLB_DIAGNOSTIC_KERNEL_MONITOR_H
#define PLB_DIAGNOSTIC_KERNEL_MONITOR_H
#include <errno.h>
#include <string.h>
#include <unistd.h>
static inline int plb_diagnostic_kernel_bad(int fd) {
    char record[8192];
    for (int i = 0; i < 256; i++) {
        ssize_t count = read(fd, record, sizeof(record) - 1);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        if (count <= 0) {
            return 1;
        }
        record[count] = 0;
        if (strstr(record, "BUG:") || strstr(record, "Oops:") || strstr(record, "Kernel panic") ||
            strstr(record, "still mapped when deleted")) {
            return 1;
        }
    }
    return 1;
}

#endif
