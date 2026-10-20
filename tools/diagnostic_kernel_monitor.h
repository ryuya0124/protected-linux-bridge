/* MIT. Observe only newly appended kernel records; no logging policy changes. */
#ifndef PLB_DIAGNOSTIC_KERNEL_MONITOR_H
#define PLB_DIAGNOSTIC_KERNEL_MONITOR_H
#include <errno.h>
#include <string.h>
#include <unistd.h>
static inline int plb_diagnostic_kernel_bad_text(const char *record, int known_host17_traces) {
    if (!record || strstr(record, "Oops:") || strstr(record, "Kernel panic") ||
        strstr(record, "still mapped when deleted"))
        return 1;
    if (!strstr(record, "BUG:"))
        return 0;
    if (known_host17_traces) {
        const char *message = strchr(record, ';');
        message = message ? message + 1 : record;
        /* The exact pinned ROM's key_thread_write unconditionally prints these
         * strings via printk on its normal successful proc-write path. They
         * are not a BUG()/WARN() site. Never ignore a prefix or extra text. */
        const char *traces[] = {"BUG:svprt key_thread_write", "BUG:svprt write=0",
                                "BUG:svprt write=1"};
        size_t length = strlen(message);
        if (length && message[length - 1] == '\n')
            length--;
        for (size_t i = 0; i < sizeof(traces) / sizeof(*traces); i++)
            if (strlen(traces[i]) == length && !memcmp(message, traces[i], length))
                return 0;
    }
    return 1;
}

static inline int plb_diagnostic_kernel_bad_with_traces(int fd, int known_host17_traces) {
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
        if (plb_diagnostic_kernel_bad_text(record, known_host17_traces)) {
            return 1;
        }
    }
    return 1;
}

static inline int plb_diagnostic_kernel_bad(int fd) {
    return plb_diagnostic_kernel_bad_with_traces(fd, 0);
}

#endif
