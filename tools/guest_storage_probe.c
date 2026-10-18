/* MIT. Read-only comparison of file data before and after offline disk maintenance. */
#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int node(const char *name) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/block/%s/dev", name);
    FILE *file = fopen(path, "r");
    unsigned major, minor;
    if (!file || fscanf(file, "%u:%u", &major, &minor) != 2) {
        if (file)
            fclose(file);
        return -1;
    }
    fclose(file);
    snprintf(path, sizeof(path), "/dev/%s", name);
    return mknod(path, S_IFBLK | 0600, makedev(major, minor));
}

static int manifest(const char *root, char result[65]) {
    int pipes[2];
    if (pipe(pipes))
        return -1;
    pid_t child = fork();
    if (!child) {
        close(pipes[0]);
        dup2(pipes[1], STDOUT_FILENO);
        close(pipes[1]);
        if (chroot(root) || chdir("/"))
            _exit(125);
        clearenv();
        setenv("PATH", "/usr/bin:/bin", 1);
        setenv("LC_ALL", "C", 1);
        alarm(120);
        /* No guest mounts are under the scanned trees. Paths/NUL framing are stable. */
        execl("/bin/bash", "bash", "-o", "pipefail", "-c",
              "find /usr /etc /root /var /home /opt /srv /boot -xdev -type f -print0 | "
              "sort -z | xargs -0 sha256sum | sha256sum",
              (char *)NULL);
        _exit(127);
    }
    close(pipes[1]);
    if (child < 0) {
        close(pipes[0]);
        return -1;
    }
    size_t count = 0;
    while (count < 64) {
        ssize_t n = read(pipes[0], result + count, 64 - count);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        count += (size_t)n;
    }
    close(pipes[0]);
    int status;
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR)
            return -1;
    result[64] = 0;
    return count == 64 && WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
}

int main(void) {
    if (getpid() != 1)
        return 99;
    int console = open("/dev/console", O_RDWR);
    if (console < 0)
        return 98;
    for (int i = 0; i < 3; i++)
        dup2(console, i);
    setvbuf(stdout, NULL, _IONBF, 0);
    int error = mount("proc", "/proc", "proc", 0, NULL) ||
                mount("sysfs", "/sys", "sysfs", 0, NULL) || node("vda") || node("vdb") ||
                mount("/dev/vda", "/newroot", "ext4", MS_RDONLY, "noload") ||
                mount("tmpfs", "/newroot/tmp", "tmpfs", MS_NOSUID, "mode=1777,size=8m") ||
                mkdir("/newroot/tmp/after", 0700) ||
                mount("/dev/vdb", "/newroot/tmp/after", "ext4", MS_RDONLY, "noload");
    if (!error) {
        const char *roots[] = {"/newroot/dev", "/newroot/tmp/after/dev"};
        for (int i = 0; i < 2; i++) {
            char null_path[128];
            if (mount("tmpfs", roots[i], "tmpfs", MS_NOSUID, "mode=755,size=1m")) {
                error = 1;
                break;
            }
            snprintf(null_path, sizeof(null_path), "%s/null", roots[i]);
            if (mknod(null_path, S_IFCHR | 0666, makedev(1, 3))) {
                error = 1;
                break;
            }
        }
    }
    char before[65] = {0}, after[65] = {0};
    if (!error)
        error = manifest("/newroot", before) || manifest("/newroot/tmp/after", after) ||
                strcmp(before, after);
    printf("PLB_FILE_MANIFEST_BEFORE=%s\nPLB_FILE_MANIFEST_AFTER=%s\n", before, after);
    puts(error ? "PLB_FILE_MANIFEST_FAILED" : "PLB_FILE_MANIFEST_MATCHED");
    fflush(stdout);
    reboot(RB_POWER_OFF);
    for (;;)
        pause();
}
