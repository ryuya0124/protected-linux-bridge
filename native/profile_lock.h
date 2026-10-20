/* MIT. One owned manager or offline profile copy at a time, across all profiles. */
#ifndef PLB_PROFILE_LOCK_H
#define PLB_PROFILE_LOCK_H
#include <sys/file.h>
#include <stdio.h>
#include <errno.h>
#ifndef PLB_OWNER_LOCK_FILE
#define PLB_OWNER_LOCK_FILE "/data/user/0/net.ryuya_dev.protectedlinux/files/vm-owner.lock"
#endif
/* A manager or short settings command holds a shared maintenance lease.
 * The archive worker holds the exclusive lease, including recovery. */
static inline int profile_maintenance_lock(int operation) {
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s.maintenance", PLB_OWNER_LOCK_FILE);
    if (length < 0 || (size_t)length >= sizeof(path))
        return -1;
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat st;
    if (fd < 0)
        return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
        (st.st_mode & 077) || flock(fd, operation | LOCK_NB)) {
        close(fd);
        return -1;
    }
    if (operation == LOCK_SH) {
        char marker[1024];
        length = snprintf(marker, sizeof(marker), "%s.restore-pending", PLB_OWNER_LOCK_FILE);
        if (length < 0 || (size_t)length >= sizeof(marker) || !lstat(marker, &st) ||
            errno != ENOENT) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static int profile_lock(void) {
    int fd = open(PLB_OWNER_LOCK_FILE, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
        (st.st_mode & 077) || flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd);
        return -1;
    }
#ifndef PLB_BACKUP_RECOVERY_LOCK
    /* Recheck after acquiring the flock: a killed restorer leaves this durable
     * marker until the dedicated recovery path rolls back every promoted root. */
    char marker[1024];
    int length = snprintf(marker, sizeof(marker), "%s.restore-pending", PLB_OWNER_LOCK_FILE);
    if (length < 0 || (size_t)length >= sizeof(marker) || !lstat(marker, &st) || errno != ENOENT) {
        close(fd);
        return -1;
    }
#endif
    return fd;
}
#endif
