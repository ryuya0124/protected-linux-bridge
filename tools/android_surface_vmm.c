/* MIT. The separately pinned VMM executable is a fixed app_process/JNI launcher.
 * All VM arguments must equal the existing guarded profile's compiled arguments.
 * It preserves the prepared normal-RAM memfd contract and every protection flag.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#include "android4g_config.h"
#define VMM PLB_ANDROID4G_VMM
#define INITRAMFS PLB_ANDROID4G_BASE "/android-lowram.cpio"
#define GUEST_KERNEL PLB_ANDROID4G_KERNEL
#define PLB_PREALLOCATED_ANDROID_PROXY 1
#include "android4g_arguments.h"
#include "android_surface.generated.h"

static int pinned(const char *path, const char *expected) {
    struct stat file;
    if (lstat(path, &file) || !S_ISREG(file.st_mode) || file.st_uid || file.st_nlink != 1 ||
        (file.st_mode & 077))
        return 0;
    int channel[2];
    if (pipe2(channel, O_CLOEXEC))
        return 0;
    pid_t pid = fork();
    if (!pid) {
        dup2(channel[1], STDOUT_FILENO);
        close(channel[0]);
        close(channel[1]);
        execl("/system/bin/toybox", "toybox", "sha256sum", path, (char *)NULL);
        _exit(126);
    }
    close(channel[1]);
    char text[512];
    ssize_t size = read(channel[0], text, sizeof(text));
    close(channel[0]);
    if (pid < 0)
        return 0;
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && size > 65 &&
           !memcmp(text, expected, 64) && text[64] == ' ';
}

int main(int argc, char **argv) {
    size_t expected = sizeof(android4g_arguments) / sizeof(*android4g_arguments) - 1;
    if (getuid() || argc != (int)expected)
        return 20;
    for (size_t i = 0; i < expected; i++)
        if (strcmp(argv[i], android4g_arguments[i]))
            return 21;
    if (!pinned(FILES "/kernel-probes/libcrosvm-surface.so", PLB_SURFACE_LIBRARY_SHA256) ||
        !pinned(FILES "/kernel-probes/libsurface-probe.so", PLB_SURFACE_JNI_SHA256) ||
        !pinned(FILES "/kernel-probes/surface-probe.apk", PLB_SURFACE_APK_SHA256))
        return 22;
    if (setenv("CLASSPATH", FILES "/kernel-probes/surface-probe.apk", 1))
        return 23;
    char *java[168] = {"/system/bin/app_process", "-Xmx64m", "/system/bin",
                       "net.ryuya_dev.protectedlinux.surfaceprobe.RootSurfaceVmm"};
    for (size_t i = 0; i < expected; i++)
        java[4 + i] = argv[i];
    java[4 + expected] = NULL;
    execv(java[0], java);
    return 24;
}
