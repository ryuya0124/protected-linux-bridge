/* MIT. Install only compiled, hashed app assets. Never creates or replaces OS disks. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#include "profile_lock.h"
#include "android_gpu4g_files.h"
#include "android17_install.generated.h"
int main(void) {
    struct stat application;
    if (getuid() || lstat("/data/user/0/net.ryuya_dev.protectedlinux", &application) ||
        !S_ISDIR(application.st_mode) || application.st_uid < 10000 || application.st_uid > 19999)
        return 20;
    int maintenance = profile_maintenance_lock(LOCK_SH), owner = profile_lock();
    if (maintenance < 0 || owner < 0)
        return 28;
    if (owned_directory(FILES "/android17-app-runtime", 1))
        return 21;
    for (size_t i = 0; i < sizeof(runtime_items) / sizeof(*runtime_items); i++) {
        char source[512], destination[512];
        snprintf(source, sizeof(source), FILES "/runtime/android17-app-%s", runtime_items[i].name);
        snprintf(destination, sizeof(destination), FILES "/android17-app-runtime/%s",
                 runtime_items[i].name);
        if (owned_install_pinned(source, destination, runtime_items[i].sha256, 0500,
                                 application.st_uid))
            return 31;
    }
    int directory =
        open(FILES "/android17-app-runtime", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0 || fsync(directory))
        return 32;
    close(directory);
    puts("ANDROID17_APP_RUNTIME_INSTALLED");
    return 0;
}
