/* MIT. Offline copy into one fresh, fixed Android 17 test profile. Never replaces a profile. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "profile_lock.h"
#define ROOT "/data/user/0/net.ryuya_dev.protectedlinux/files/android-guests"
static int copy(const char *source, const char *destination) {
    struct stat info;
    if (lstat(source, &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink != 1 ||
        (info.st_mode & 077))
        return 1;
    int input = open(source, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    int output = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (input < 0 || output < 0)
        return 2;
    char bytes[65536];
    for (;;) {
        ssize_t count = read(input, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            return 3;
        if (!count)
            break;
        ssize_t at = 0;
        while (at < count) {
            ssize_t written = write(output, bytes + at, count - at);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                return 4;
            at += written;
        }
    }
    struct stat after;
    int valid = !fstat(input, &after) && after.st_dev == info.st_dev &&
                after.st_ino == info.st_ino && after.st_size == info.st_size &&
                after.st_mtim.tv_sec == info.st_mtim.tv_sec &&
                after.st_mtim.tv_nsec == info.st_mtim.tv_nsec && !fsync(output);
    close(input);
    close(output);
    return valid ? 0 : 5;
}
int main(void) {
    if (getuid())
        return 40;
    int maintenance = profile_maintenance_lock(LOCK_SH), owner = profile_lock();
    if (maintenance < 0 || owner < 0)
        return 41;
    if (mkdir(ROOT "/h17gles.preparing", 0700))
        return 42;
    const char *names[] = {"android-work.qcow2", "android-misc.raw", "android-frp.raw"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
        char source[512], destination[512];
        snprintf(source, sizeof(source), ROOT "/cuttlefish17/%s", names[i]);
        snprintf(destination, sizeof(destination), ROOT "/h17gles.preparing/%s", names[i]);
        if (copy(source, destination))
            return 43;
    }
    if (copy("/data/user/0/net.ryuya_dev.protectedlinux/files/android17-owned-runtime/"
             "android-lowram.cpio",
             ROOT "/h17gles.preparing/android-lowram.cpio"))
        return 44;
    struct stat old;
    if (!lstat(ROOT "/h17gles", &old) || errno != ENOENT)
        return 45;
    if (rename(ROOT "/h17gles.preparing", ROOT "/h17gles"))
        return 46;
    int parent = open(ROOT, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent < 0 || fsync(parent))
        return 47;
    close(parent);
    close(owner);
    close(maintenance);
    puts("ANDROID17_SEPARATE_PROFILE_CREATED");
    return 0;
}
