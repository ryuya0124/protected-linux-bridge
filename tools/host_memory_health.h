/* MIT. Refuse new diagnostic load after known host page-management failures.
 * Root's expected out-of-tree/warning taints alone are not rejected. A clear
 * value is only an admission check, never a proof that a hypervisor is healthy. */
#ifndef PLB_HOST_MEMORY_HEALTH_H
#define PLB_HOST_MEMORY_HEALTH_H
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

static inline int plb_host_memory_healthy_text(const char *text) {
    if (!text || *text < '0' || *text > '9')
        return 0;
    errno = 0;
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || (*end && !(*end == '\n' && !end[1])))
        return 0;
    const unsigned long bad_page_or_oops = (1UL << 5) | (1UL << 7);
    return !(value & bad_page_or_oops);
}

static inline int plb_host_memory_healthy(void) {
    int fd = open("/proc/sys/kernel/tainted", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return 0;
    char text[64];
    ssize_t size = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (size <= 0 || size == (ssize_t)sizeof(text) - 1)
        return 0;
    text[size] = 0;
    return plb_host_memory_healthy_text(text);
}
#endif
