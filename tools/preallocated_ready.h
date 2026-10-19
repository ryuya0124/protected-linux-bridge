/* MIT. A single owned pipe byte signals completed preparation, never approval.
 * Missing notification is supported for standalone diagnostics. A supplied but
 * invalid channel refuses VM exec; it is not silently ignored.
 */
#ifndef PLB_PREALLOCATED_READY_H
#define PLB_PREALLOCATED_READY_H
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define PLB_PREALLOCATED_READY_ENV "PLB_PREALLOCATED_READY_FD"

static inline int plb_ready_descriptor(const char *text, uid_t owner) {
    if (!text || !*text) {
        return -1;
    }
    char *end;
    errno = 0;
    long number = strtol(text, &end, 10);
    if (errno || *end || number < 3 || number > 1024 || number == 32) {
        return -1;
    }
    int fd = (int)number;
    struct stat info;
    int flags = fcntl(fd, F_GETFL);
    return !fstat(fd, &info) && S_ISFIFO(info.st_mode) && info.st_uid == owner && flags >= 0 &&
                   (flags & O_ACCMODE) == O_WRONLY
               ? fd
               : -1;
}

static inline int plb_preallocated_notify_ready(void) {
    const char *text = getenv(PLB_PREALLOCATED_READY_ENV);
    if (!text) {
        return 0;
    }
    int fd = plb_ready_descriptor(text, 0);
    unsetenv(PLB_PREALLOCATED_READY_ENV);
    if (fd < 0) {
        return -1;
    }
    ssize_t size;
    do {
        size = write(fd, "R", 1);
    } while (size < 0 && errno == EINTR);
    close(fd);
    return size == 1 ? 0 : -1;
}
#endif
