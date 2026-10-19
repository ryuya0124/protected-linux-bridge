/* MIT. Hold the native owner lock across the fixed root-owned copy script.
 * Android toybox flock supports only an inherited FD; do not use GNU syntax.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "diagnostic_owner_lock.h"
#include "probe_watchdog.h"

int main(int argc, char **argv) {
    int custom = argc == 2 && (!strcmp(argv[1], "--custom-guest") ||
                               !strcmp(argv[1], "--archive-custom-boot-complete") ||
                               !strcmp(argv[1], "--archive-custom-normal-first") ||
                               !strcmp(argv[1], "--archive-custom-capsets") ||
                               !strcmp(argv[1], "--archive-custom-loader") ||
                               !strcmp(argv[1], "--archive-custom-renderer-feature") ||
                               !strcmp(argv[1], "--archive-custom-binder-diagnosis") ||
                               !strcmp(argv[1], "--archive-custom-system-heap") ||
                               !strcmp(argv[1], "--archive-custom-dma-pool") ||
                               !strcmp(argv[1], "--archive-custom-keyguard"));
    int archive =
        argc == 2 &&
        (!strcmp(argv[1], "--archive-normal-first") || !strcmp(argv[1], "--archive-ascending") ||
         !strcmp(argv[1], "--archive-medium-only") ||
         !strcmp(argv[1], "--archive-medium-aligned") ||
         !strcmp(argv[1], "--archive-all-aligned") || !strcmp(argv[1], "--archive-pipe-trace"));
    if (getuid() || (argc != 1 && !archive && !custom))
        return 20;
    int lock = diagnostic_owner_lock();
    if (lock < 0)
        return 21;
    const char *script = "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/"
                         "prepare-android4g-profile.sh";
    struct stat info;
    if (lstat(script, &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink != 1 ||
        (info.st_mode & 0022)) {
        close(lock);
        return 25;
    }
    pid_t parent = getpid(), child = fork();
    if (!child) {
        char descriptor[16];
        snprintf(descriptor, sizeof(descriptor), "%d", lock);
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent || fcntl(lock, F_SETFD, 0) ||
            setenv("PLB_ANDROID4G_COPY_FD", descriptor, 1))
            _exit(22);
        if (archive || custom)
            execl("/system/bin/sh", "sh", script, argv[1], (char *)NULL);
        else
            execl("/system/bin/sh", "sh", script, (char *)NULL);
        _exit(23);
    }
    int result = child < 0 ? 24 : plb_probe_wait(child, 90000);
    close(lock);
    return result;
}
