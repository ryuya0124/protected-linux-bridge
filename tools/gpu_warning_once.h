/* MIT. Development-only exception for one explicitly approved 1536 MiB GLES
 * trial. Not compiled into the APK or ordinary diagnostic artifacts. Approval
 * is bound to the current boot, a five-minute window, exact existing taint and
 * one root-owned marker/open descriptor. This never clears a kernel warning. */
#ifndef PLB_GPU_WARNING_ONCE_H
#define PLB_GPU_WARNING_ONCE_H
#ifndef PLB_GPU_WARNING_ONCE
#error "This header is for the explicitly approved development build only"
#endif
#include <sys/stat.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gpu_once.generated.h"
#ifndef PLB_GPU_ONCE_MARKER
#define PLB_GPU_ONCE_MARKER                                                                        \
    "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/"                               \
    "gpu1536-warning-once.used"
#endif
#define PLB_GPU_ONCE_FD_ENV "PLB_GPU_WARNING_APPROVAL_FD"
static const char plb_gpu_once_magic[] = "PLB_GPU_ONCE 1536 GLES\n";

static inline int plb_gpu_once_values(const char *boot, unsigned long long now,
                                      unsigned long taint) {
    return boot && !strcmp(boot, PLB_GPU_ONCE_BOOT_ID) &&
           PLB_GPU_ONCE_DEADLINE_MS > PLB_GPU_ONCE_ISSUED_MS &&
           PLB_GPU_ONCE_DEADLINE_MS - PLB_GPU_ONCE_ISSUED_MS <= 300000ULL &&
           now >= PLB_GPU_ONCE_ISSUED_MS && now < PLB_GPU_ONCE_DEADLINE_MS &&
           taint == PLB_GPU_ONCE_TAINT && taint == 4640 && !(taint & (1UL << 7));
}

static inline int plb_gpu_once_window(void) {
    char boot[38] = {0}, text[64] = {0};
    int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t count = fd < 0 ? -1 : read(fd, boot, sizeof(boot));
    if (fd >= 0)
        close(fd);
    if (count != 37 || boot[36] != '\n')
        return 0;
    boot[36] = 0;
    fd = open("/proc/sys/kernel/tainted", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    count = fd < 0 ? -1 : read(fd, text, sizeof(text) - 1);
    if (fd >= 0)
        close(fd);
    if (count <= 0 || count == sizeof(text) - 1 || text[0] < '0' || text[0] > '9')
        return 0;
    char *end;
    errno = 0;
    unsigned long taint = strtoul(text, &end, 10);
    if (errno || (*end && !(*end == '\n' && !end[1])))
        return 0;
    struct timespec now;
    if (clock_gettime(CLOCK_BOOTTIME, &now))
        return 0;
    return plb_gpu_once_values(boot, (unsigned long long)now.tv_sec * 1000 + now.tv_nsec / 1000000,
                               taint);
}

static inline int plb_gpu_once_proof_fd(void) {
    const char *text = getenv(PLB_GPU_ONCE_FD_ENV);
    if (!text || !*text)
        return -1;
    char *end;
    long value = strtol(text, &end, 10);
    if (*end || value < 3 || value > 1024)
        return -1;
    struct stat actual, expected;
    uid_t owner = 0;
#ifdef PLB_GPU_ONCE_TEST
    owner = getuid();
#endif
    char magic[sizeof(plb_gpu_once_magic)] = {0};
    if (fstat((int)value, &actual) || lstat(PLB_GPU_ONCE_MARKER, &expected) ||
        !S_ISREG(expected.st_mode) || expected.st_uid != owner || expected.st_nlink != 1 ||
        (expected.st_mode & 077) || actual.st_dev != expected.st_dev ||
        actual.st_ino != expected.st_ino || actual.st_size != sizeof(plb_gpu_once_magic) - 1 ||
        pread((int)value, magic, sizeof(magic), 0) != sizeof(plb_gpu_once_magic) - 1 ||
        memcmp(magic, plb_gpu_once_magic, sizeof(plb_gpu_once_magic) - 1))
        return -1;
    return (int)value;
}

static inline int plb_gpu_once_admitted(void) {
    return plb_gpu_once_window() && plb_gpu_once_proof_fd() >= 0;
}

/* Must be called by the single, already locked policy supervisor, never a UI.
 * O_EXCL is the trial counter: keep the used marker even when startup fails. */
static inline int plb_gpu_once_claim_marker(void) {
    int fd = open(PLB_GPU_ONCE_MARKER, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 3) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    if (write(fd, plb_gpu_once_magic, sizeof(plb_gpu_once_magic) - 1) !=
            sizeof(plb_gpu_once_magic) - 1 ||
        fsync(fd) || fcntl(fd, F_SETFD, 0)) {
        close(fd);
        return -1; /* Never remove/refresh the marker automatically. */
    }
    char descriptor[16];
    snprintf(descriptor, sizeof(descriptor), "%d", fd);
    if (setenv(PLB_GPU_ONCE_FD_ENV, descriptor, 1) || plb_gpu_once_proof_fd() != fd) {
        close(fd);
        return -1;
    }
    return fd;
}

static inline int plb_gpu_once_claim(void) {
    return plb_gpu_once_window() ? plb_gpu_once_claim_marker() : -1;
}

static int plb_gpu_once_kernel_fd = -1;
static inline int plb_gpu_once_watch_open(void) {
    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    if (fd < 0)
        return -1;
    if (fstat(fd, &info) || !S_ISCHR(info.st_mode) || major(info.st_rdev) != 1 ||
        minor(info.st_rdev) != 11 || lseek(fd, 0, SEEK_END) < 0) {
        close(fd);
        return -1;
    }
    plb_gpu_once_kernel_fd = fd;
    return 0;
}

static inline int plb_gpu_once_kernel_bad(void) {
    if (plb_gpu_once_kernel_fd < 0)
        return 0;
    char record[8192];
    for (int i = 0; i < 256; i++) {
        ssize_t size = read(plb_gpu_once_kernel_fd, record, sizeof(record) - 1);
        if (size < 0 && errno == EINTR)
            continue;
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        if (size <= 0)
            return 1; /* Including ring overflow: the observation was lost. */
        record[size] = 0;
        if (strstr(record, "BUG:") || strstr(record, "Oops:") || strstr(record, "Kernel panic") ||
            strstr(record, "still mapped when deleted")) {
            fprintf(stderr, "PLB_GPU_ONCE_NEW_KERNEL_FAILURE\n");
            return 1;
        }
    }
    return 1; /* Cannot keep up with the bounded monitor: fail closed. */
}
#endif
