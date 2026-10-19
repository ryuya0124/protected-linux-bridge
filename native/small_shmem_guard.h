/* MIT. Explicitly selected Android large-memory startup only.
 * Three fixed host-wide settings, never another policy. A separate guardian
 * restores them on controller loss or deadline; it signals only its own parent.
 * Call before starting threads. Keep the common owner lock held throughout. */
#ifndef PLB_SMALL_SHMEM_GUARD_H
#define PLB_SMALL_SHMEM_GUARD_H
#ifndef PLB_SHMEM_GUARD_TEST_PREFIX
#include <sys/statfs.h>
#include <linux/magic.h>
#endif
#include <poll.h>
#include <time.h>

#ifndef PLB_SHMEM_GUARD_TIMEOUT_MS
#define PLB_SHMEM_GUARD_TIMEOUT_MS 40000
#endif
#ifdef PLB_SHMEM_GUARD_TEST_PREFIX
static const char *plb_shmem_paths[] = {
    PLB_SHMEM_GUARD_TEST_PREFIX "/small-16k",
    PLB_SHMEM_GUARD_TEST_PREFIX "/small-32k",
    PLB_SHMEM_GUARD_TEST_PREFIX "/small-64k",
};
#else
static const char *plb_shmem_paths[] = {
    "/sys/kernel/mm/transparent_hugepage/hugepages-16kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-32kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-64kB/shmem_enabled",
};
#endif

struct plb_shmem_guard {
    pid_t guardian;
    int alive_write;
    long long started_ms;
};

static long long plb_shmem_millis(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return -1;
    return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

static int plb_shmem_read(const char *path, char value[32]) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    char data[256];
    ssize_t size = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (size <= 0)
        return -1;
    data[size] = 0;
#ifdef PLB_SHMEM_GUARD_TEST_PREFIX
    if (!strcmp(data, "never") || !strcmp(data, "advise") || !strcmp(data, "always")) {
        strcpy(value, data);
        return 0;
    }
#endif
    char *begin = strchr(data, '[');
    char *end = begin ? strchr(begin + 1, ']') : NULL;
    if (!begin || !end || end - begin <= 1 || end - begin > 32)
        return -1;
    size_t length = (size_t)(end - begin - 1);
    memcpy(value, begin + 1, length);
    value[length] = 0;
    return 0;
}

static int plb_shmem_write(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct stat info;
    int valid = !fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == geteuid();
#ifdef PLB_SHMEM_GUARD_TEST_PREFIX
    valid = valid && !ftruncate(fd, 0);
#else
    struct statfs filesystem;
    valid = valid && !fstatfs(fd, &filesystem) && filesystem.f_type == SYSFS_MAGIC;
#endif
    size_t length = strlen(value);
    int result = valid && write(fd, value, length) == (ssize_t)length ? 0 : -1;
    close(fd);
    char actual[32];
    return result || plb_shmem_read(path, actual) || strcmp(actual, value) ? -1 : 0;
}

static int plb_shmem_original(void) {
    for (size_t i = 0; i < 3; i++) {
        char actual[32];
        if (plb_shmem_read(plb_shmem_paths[i], actual) || strcmp(actual, "never"))
            return 0;
    }
    return 1;
}

static int plb_shmem_restore(void) {
    int failed = 0;
    for (int i = 2; i >= 0; i--) {
        char actual[32];
        if (plb_shmem_read(plb_shmem_paths[i], actual))
            failed = 1;
        else if (!strcmp(actual, "advise"))
            failed |= plb_shmem_write(plb_shmem_paths[i], "never") != 0;
        else if (strcmp(actual, "never"))
            failed = 1; /* Never overwrite another administrator's unexpected mode. */
    }
    return failed || !plb_shmem_original() ? -1 : 0;
}

static int plb_shmem_finish(struct plb_shmem_guard *guard) {
    if (guard->guardian <= 0)
        return 0;
    int restored = plb_shmem_restore() == 0;
    if (guard->alive_write >= 0)
        close(guard->alive_write);
    guard->alive_write = -1;
    int status = 0;
    pid_t done = 0;
    for (int i = 0; i < 30; i++) {
        done = waitpid(guard->guardian, &status, WNOHANG);
        if (done > 0 || (done < 0 && errno != EINTR))
            break;
        usleep(100000);
    }
    int verified = restored && done == guard->guardian && WIFEXITED(status) &&
                   WEXITSTATUS(status) == 0 && plb_shmem_original();
    if (done == guard->guardian)
        guard->guardian = -1;
    fprintf(stderr, "ANDROID_SMALL_SHMEM_RESTORED=%d\n", verified);
    return verified ? 0 : -1;
}

static int plb_shmem_begin(struct plb_shmem_guard *guard, int owner_lock) {
    guard->guardian = -1;
    guard->alive_write = -1;
    guard->started_ms = plb_shmem_millis();
    if (guard->started_ms < 0 || !plb_shmem_original())
        return -1;
    int alive[2];
    if (pipe2(alive, O_CLOEXEC))
        return -1;
    pid_t parent = getpid();
    pid_t guardian = fork();
    if (!guardian) {
        close(alive[1]);
        setsid();
        struct pollfd watched = {.fd = alive[0], .events = POLLIN | POLLHUP};
        int ready = poll(&watched, 1, PLB_SHMEM_GUARD_TIMEOUT_MS);
        if (ready <= 0 && getppid() == parent)
            kill(parent, SIGKILL);
        int restored = plb_shmem_restore() == 0;
        dprintf(STDERR_FILENO, "ANDROID_SMALL_SHMEM_GUARDIAN_RESTORED=%d\n", restored);
        close(alive[0]);
        close(owner_lock);
        _exit(restored ? 0 : 1);
    }
    close(alive[0]);
    if (guardian < 0) {
        close(alive[1]);
        return -1;
    }
    guard->guardian = guardian;
    guard->alive_write = alive[1];
    for (size_t i = 0; i < 3; i++) {
        if (plb_shmem_write(plb_shmem_paths[i], "advise")) {
            plb_shmem_finish(guard);
            return -1;
        }
    }
    fprintf(stderr, "ANDROID_SMALL_SHMEM_ACTIVE sizes_kib=16,32,64 restore_after_seconds=10\n");
    return 0;
}
#endif
