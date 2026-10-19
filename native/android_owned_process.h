/* MIT. Isolate only this root controller from its launching app's cached-app
 * freezer group before forking the restoration guardian. No freezer switch,
 * other task's membership, CPU policy or cgroup directory is changed. */
#ifndef PLB_ANDROID_OWNED_PROCESS_H
#define PLB_ANDROID_OWNED_PROCESS_H
#include <sys/statfs.h>
#include <linux/magic.h>

static int android_own_cgroup_path(char path[256]) {
    FILE *input = fopen("/proc/self/cgroup", "re");
    if (!input)
        return -1;
    char line[320];
    int found = 0;
    while (fgets(line, sizeof(line), input)) {
        if (strncmp(line, "0::", 3))
            continue;
        size_t length = strcspn(line + 3, "\n");
        if (found || !length || length >= 256) {
            found = -1;
            break;
        }
        memcpy(path, line + 3, length);
        path[length] = 0;
        found = 1;
    }
    fclose(input);
    return found == 1 ? 0 : -1;
}

static int android_isolate_owned_controller(uid_t app_uid) {
    char path[256];
    if (android_own_cgroup_path(path))
        return -1;
    if (!strcmp(path, "/"))
        return 0;
    unsigned uid = 0;
    long app_pid = 0;
    int consumed = 0;
    if (sscanf(path, "/apps/uid_%u/pid_%ld%n", &uid, &app_pid, &consumed) != 2 || path[consumed] ||
        uid != app_uid || app_pid <= 0)
        return -1;
    int fd = open("/sys/fs/cgroup/cgroup.procs", O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct stat info;
    struct statfs filesystem;
    int valid = !fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == 1000 &&
                !fstatfs(fd, &filesystem) && filesystem.f_type == CGROUP2_SUPER_MAGIC;
    char own_pid[32];
    int length = snprintf(own_pid, sizeof(own_pid), "%ld", (long)getpid());
    int moved = valid && write(fd, own_pid, (size_t)length) == length;
    close(fd);
    if (!moved || android_own_cgroup_path(path) || strcmp(path, "/"))
        return -1;
    fprintf(stderr, "ANDROID_OWNED_CONTROLLER_ISOLATED=1\n");
    return 0;
}
#endif
