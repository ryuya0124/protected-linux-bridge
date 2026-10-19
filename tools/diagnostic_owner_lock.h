/* MIT. Borrow only the validated owner-lock descriptor from a diagnostic parent.
 * Production managers do not use this API. Callers enforce root/device identity. */
#ifndef PLB_DIAGNOSTIC_OWNER_LOCK_H
#define PLB_DIAGNOSTIC_OWNER_LOCK_H
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include "../native/profile_lock.h"

#define PLB_DIAGNOSTIC_OWNER_ENV "PLB_DIAGNOSTIC_OWNER_FD"

static int diagnostic_owner_lock(void) {
    const char *text = getenv(PLB_DIAGNOSTIC_OWNER_ENV);
    if (!text)
        return profile_lock();
    char *end;
    long descriptor = strtol(text, &end, 10);
    int valid_number = *text && !*end && descriptor >= 3 && descriptor <= 1024;
    unsetenv(PLB_DIAGNOSTIC_OWNER_ENV);
    if (!valid_number)
        return -1;
    int fd = (int)descriptor;
    struct stat inherited, expected;
    if (fstat(fd, &inherited) || lstat(PLB_OWNER_LOCK_FILE, &expected) ||
        !S_ISREG(expected.st_mode) || expected.st_uid != geteuid() || expected.st_nlink != 1 ||
        (expected.st_mode & 077) || inherited.st_dev != expected.st_dev ||
        inherited.st_ino != expected.st_ino || fcntl(fd, F_SETFD, FD_CLOEXEC) ||
        flock(fd, LOCK_EX | LOCK_NB))
        return -1;
    return fd;
}
#endif
