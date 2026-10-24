/* MIT. Read-only kernel-monitor diagnosis; never starts a guest or changes policy. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../diagnostic_kernel_monitor.h"

static long long milliseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

int main(void) {
    if (getuid() != 0) {
        return 40;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || lseek(fd, 0, SEEK_END) < 0) {
        printf("MONITOR_CAPTURE admission_errno=%d\n", errno);
        return 81;
    }
    unsigned records = 0, known = 0, unknown = 0, budgets = 0;
    long long end = milliseconds() + 12000;
    while (milliseconds() < end) {
        unsigned batch = 0;
        for (; batch < 256; batch++) {
            char record[8192];
            ssize_t count = read(fd, record, sizeof(record) - 1);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (count <= 0) {
                printf("MONITOR_CAPTURE read_errno=%d records=%u\n", errno, records);
                close(fd);
                return 88;
            }
            record[count] = 0;
            records++;
            if (plb_diagnostic_kernel_bad_text(record, 0)) {
                int still_bad = plb_diagnostic_kernel_bad_text(record, 1);
                if (still_bad) {
                    unknown++;
                } else {
                    known++;
                }
                printf("MONITOR_CAPTURE strict_stop=1 known_exact_trace=%d record=%s", !still_bad,
                       record);
            }
        }
        if (batch == 256) {
            budgets++;
            printf("MONITOR_CAPTURE budget_exhausted=1\n");
        }
        usleep(50000);
    }
    close(fd);
    printf("MONITOR_CAPTURE records=%u known=%u unknown=%u budgets=%u vm_started=0\n", records,
           known, unknown, budgets);
    return unknown || budgets ? 88 : 0;
}
