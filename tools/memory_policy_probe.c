/* MIT. Development-only proposed runtime-policy trial; not included in the APK.
 * --plan is read-only. --apply-approved must not be run without explicit human
 * authorization for the two host-wide settings documented in the proposal.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/file.h>
#include <sys/system_properties.h>
#include <linux/magic.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#define PLB_OWNER_LOCK_FILE FILES "/vm-owner.lock"
#include "../native/profile_lock.h"

#ifdef PLB_POLICY_MOCK
#define APPLY_OPTION "--run-mock"
static const char *settings[] = {
    FILES "/kernel-probes/policy-mock/first",
    FILES "/kernel-probes/policy-mock/second",
};
#else
#define APPLY_OPTION "--apply-approved"
static const char *settings[] = {
    "/sys/kernel/mm/transparent_hugepage/hugepages-16kB/enabled",
    "/sys/kernel/mm/transparent_hugepage/defrag",
};
#endif
static const char *probe = FILES "/kernel-probes/backing-allocation-probe";
static volatile sig_atomic_t interrupted;
static void stop(int number) {
    (void)number;
    interrupted = 1;
}

static int read_mode(const char *path, char value[32]) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    char buffer[256];
    if (fd < 0)
        return -1;
    ssize_t size = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (size <= 0)
        return -1;
    buffer[size] = 0;
    char *begin = strchr(buffer, '['), *end;
    if (!begin) {
#ifdef PLB_POLICY_MOCK
        if (!strcmp(buffer, "never") || !strcmp(buffer, "madvise") || !strcmp(buffer, "always")) {
            strcpy(value, buffer);
            return 0;
        }
#endif
        return -1;
    }
    if (!(end = strchr(++begin, ']')) || end - begin >= 32)
        return -1;
    memcpy(value, begin, (size_t)(end - begin));
    value[end - begin] = 0;
    return 0;
}

static int write_mode(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    struct statfs fs;
    struct stat st;
    int valid = !fstat(fd, &st) && S_ISREG(st.st_mode) && !st.st_uid;
#ifdef PLB_POLICY_MOCK
    (void)fs;
    valid = valid && !ftruncate(fd, 0);
#else
    valid = valid && !fstatfs(fd, &fs) && fs.f_type == SYSFS_MAGIC;
#endif
    size_t size = strlen(value);
    int result = valid && write(fd, value, size) == (ssize_t)size ? 0 : -1;
    close(fd);
    char actual[32];
    if (result || read_mode(path, actual) || strcmp(actual, value))
        return -1;
    return 0;
}

static int restore(void) {
    int failed = 0;
    for (int i = 1; i >= 0; i--) {
        char actual[32];
        if (read_mode(settings[i], actual)) {
            failed = 1;
        } else if (!strcmp(actual, "madvise")) {
            if (write_mode(settings[i], "never"))
                failed = 1;
        } else if (strcmp(actual, "never")) {
            /* Do not clobber an unrelated concurrent administrator change. */
            failed = 1;
        }
    }
    return failed;
}

static pid_t sample(pid_t child, int *status) {
    pid_t result;
    do {
        result = waitpid(child, status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    return result;
}

static int unchanged_host(const char *boot) {
    char current[80] = {0}, selinux[2] = {0};
    int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ssize_t size = read(fd, current, sizeof(current) - 1);
    close(fd);
    fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ssize_t enforce = read(fd, selinux, 1);
    close(fd);
    return size > 0 && enforce == 1 && selinux[0] == '1' && !strcmp(current, boot);
}

int main(int argc, char **argv) {
    char model[PROP_VALUE_MAX], sdk[PROP_VALUE_MAX];
    if (getuid() || argc != 2 || (strcmp(argv[1], "--plan") && strcmp(argv[1], APPLY_OPTION)))
        return 20;
#ifndef PLB_POLICY_MOCK
    if (!strcmp(argv[1], APPLY_OPTION)) {
        puts("PLB_LEGACY_POLICY_APPLY_DISABLED: see small-shmem-policy proposal");
        return 38;
    }
#endif
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.version.sdk", sdk);
    if (strcmp(model, "SM-S948Q") || strcmp(sdk, "36"))
        return 21;
    char boot[80] = {0};
    int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 22;
    ssize_t size = read(fd, boot, sizeof(boot) - 1);
    close(fd);
    if (size <= 0 || !unchanged_host(boot))
        return 22;
    for (int i = 0; i < 2; i++) {
        char mode[32];
        if (read_mode(settings[i], mode) || strcmp(mode, "never"))
            return 23;
        printf("PLB_POLICY_PLAN index=%d original=never proposed=madvise\n", i);
    }
    if (!strcmp(argv[1], "--plan"))
        return 0;
    struct stat executable;
    if (lstat(probe, &executable) || !S_ISREG(executable.st_mode) || executable.st_uid ||
        executable.st_nlink != 1 || executable.st_mode & 077)
        return 24;
    int lock = profile_lock();
    if (lock < 0)
        return 28;
    int alive[2];
    if (pipe2(alive, O_CLOEXEC))
        return 25;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGCHLD, SIG_DFL);
    fflush(NULL);
    pid_t controller = getpid();
    pid_t guardian = fork();
    if (!guardian) {
        close(alive[1]);
        /* Survive controller/session loss; never adopt a VM or another PID. */
        setsid();
        struct pollfd watching = {.fd = alive[0], .events = POLLIN | POLLHUP};
        int ready = poll(&watching, 1, 20000);
        if (ready <= 0 && getppid() == controller)
            kill(controller, SIGKILL); /* Owned controller only, before restoration. */
        int failed = restore();
        dprintf(STDERR_FILENO, "PLB_POLICY_GUARDIAN_RESTORED=%d\n", !failed);
        close(alive[0]);
        close(lock);
        _exit(failed ? 30 : 0);
    }
    close(alive[0]);
    if (guardian < 0) {
        close(alive[1]);
        return 26;
    }
    int result = 0;
    pid_t child = -1;
    if (write_mode(settings[0], "madvise") || write_mode(settings[1], "madvise")) {
        result = 27;
    } else {
#ifdef PLB_POLICY_MOCK_KILL
        kill(getpid(), SIGKILL);
#endif
        pid_t parent = getpid();
        child = fork();
        if (!child) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent)
                _exit(31);
            execl(probe, probe, "anon", (char *)NULL);
            _exit(32);
        }
        if (child < 0) {
            result = 29;
        } else {
            int status = 0;
            pid_t done = 0;
            for (int i = 0; i < 100 && !interrupted; i++) {
                done = sample(child, &status);
                if (done)
                    break;
                usleep(100000);
            }
            if (!done) {
                kill(child, SIGKILL);
                result = 124;
            } else {
                result = done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 33;
                child = -1; /* Reaped: never signal this numeric PID again. */
            }
        }
    }
    int failed = restore();
    printf("PLB_POLICY_RESTORED=%d HOST_BOUNDARY_UNCHANGED=%d\n", !failed, unchanged_host(boot));
    close(alive[1]); /* Guardian verifies/restores, even if this process now dies. */
    if (child > 0) {
        int status;
        for (int i = 0; i < 20 && sample(child, &status) == 0; i++)
            usleep(100000);
    }
    int status;
    for (int i = 0; i < 30 && sample(guardian, &status) == 0; i++)
        usleep(100000);
    close(lock);
    return failed || !unchanged_host(boot) ? 34 : result;
}
