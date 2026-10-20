/* MIT. Root-only flock shared with every Bridge VM manager. No filesystem extraction here. */
#define _GNU_SOURCE
#define PLB_BACKUP_RECOVERY_LOCK 1
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../profile_lock.h"

static int maintenance_fd = -1;

JNIEXPORT jint JNICALL Java_net_ryuya_1dev_protectedlinux_backuproot_BackupRoot_lock(JNIEnv *env,
                                                                                     jclass type) {
    (void)env;
    (void)type;
    if (getuid())
        return -1;
    umask(077);
    int owner = profile_lock();
    if (owner < 0)
        return -1;
    maintenance_fd = profile_maintenance_lock(LOCK_EX);
    if (maintenance_fd < 0) {
        close(owner);
        return -1;
    }
    return owner;
}
JNIEXPORT void JNICALL Java_net_ryuya_1dev_protectedlinux_backuproot_BackupRoot_unlock(JNIEnv *env,
                                                                                       jclass type,
                                                                                       jint fd) {
    (void)env;
    (void)type;
    if (maintenance_fd >= 0) {
        close(maintenance_fd);
        maintenance_fd = -1;
    }
    if (fd >= 0)
        close(fd);
}
