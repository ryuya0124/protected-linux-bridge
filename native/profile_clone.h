/* MIT. Separate experimental profile. The original disk is never opened writable. */
#ifndef PLB_PROFILE_CLONE_H
#define PLB_PROFILE_CLONE_H
#define STOCK_BASE "/data/user/0/net.ryuya_dev.protectedlinux/files/vm"
#define plb_read_exact disk_read_exact
#include "disk_copy.h"
#undef plb_read_exact
#define PLB_KERNEL_SHA "95fe64403ffd031ce1ba8965098eed3073ae6db38be017801d1d77117f966dbc"
static int kernel_hash_ok(int fd) {
    int pipes[2];
    if (pipe2(pipes, O_CLOEXEC))
        return 0;
    pid_t parent = getpid(), child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(1);
        int copy = fcntl(fd, F_DUPFD_CLOEXEC, 64);
        if (copy < 0)
            _exit(1);
        dup2(pipes[1], STDOUT_FILENO);
        close(pipes[0]);
        close(pipes[1]);
        if (dup2(copy, 9) < 0 || fcntl(9, F_SETFD, 0) < 0)
            _exit(1);
        close(copy);
        execl("/system/bin/sha256sum", "sha256sum", "/proc/self/fd/9", (char *)NULL);
        _exit(1);
    }
    close(pipes[1]);
    if (child < 0) {
        close(pipes[0]);
        return 0;
    }
    char text[160] = {0};
    size_t size = 0;
    double deadline = plb_now() + 5;
    int complete = 0;
    while (size < sizeof(text)) {
        int left = (int)((deadline - plb_now()) * 1000);
        struct pollfd ready = {pipes[0], POLLIN, 0};
        if (left <= 0 || poll(&ready, 1, left) <= 0)
            break;
        ssize_t n = read(pipes[0], text + size, sizeof(text) - size);
        if (!n) {
            complete = 1;
            break;
        }
        if (n < 0)
            break;
        size += (size_t)n;
    }
    close(pipes[0]);
    int status = 0;
    pid_t done = waitpid(child, &status, WNOHANG);
    for (int i = 0; !done && i < 20; i++) {
        usleep(10000);
        done = waitpid(child, &status, WNOHANG);
    }
    if (!done) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        return 0;
    }
    return complete && size >= 65 && done > 0 && WIFEXITED(status) && !WEXITSTATUS(status) &&
           !memcmp(text, PLB_KERNEL_SHA, 64) && text[64] == ' ';
}
static int kernel_ready(void) {
    int fd = open(KERNEL_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return 1;
    struct stat st;
    unsigned char magic[4];
    int bad = fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || st.st_nlink != 1 ||
              ((st.st_mode & 0777) != 0400) || st.st_size != 17514504 ||
              pread(fd, magic, 4, 56) != 4 || memcmp(magic, "ARM\144", 4);
    if (!bad)
        bad = !kernel_hash_ok(fd);
    close(fd);
    return !!bad;
}
static int profile_copy(const char *source, const char *target) {
    int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0)
        return -1;
    struct stat before, after;
    if (fstat(in, &before) || !S_ISREG(before.st_mode) || before.st_uid || before.st_size < 1) {
        close(in);
        return -1;
    }
    int out = open(target, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (out < 0) {
        close(in);
        return -1;
    }
    int error = ftruncate(out, before.st_size) ||
                disk_copy_verify(in, out, (uint64_t)before.st_size) || fstat(in, &after) ||
                !same_file(&before, &after);
    close(out);
    close(in);
    return error ? -1 : 0;
}
static int kernel_install(uid_t module_uid) {
    const char parent[] = "/data/user/0/net.ryuya_dev.protectedlinux/files/kernels",
               folder[] = "linux-6.18.57-plb1";
    if (mkdir(parent, 0700) && errno != EEXIST)
        return 1;
    int dir = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (dir < 0)
        return 1;
    if (fstat(dir, &st) || st.st_uid || (st.st_mode & 077)) {
        close(dir);
        return 1;
    }
    if (mkdirat(dir, folder, 0700) && errno != EEXIST) {
        close(dir);
        return 1;
    }
    int target = openat(dir, folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    close(dir);
    if (target < 0)
        return 1;
    if (fstat(target, &st) || st.st_uid || (st.st_mode & 077)) {
        close(target);
        return 1;
    }
    int in = open("/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-import.pending",
                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0) {
        close(target);
        return 1;
    }
    int bad = fstat(in, &st) || !S_ISREG(st.st_mode) || st.st_uid != module_uid ||
              st.st_size != 17514504 || !kernel_hash_ok(in);
    if (bad) {
        close(in);
        close(target);
        return 1;
    }
    if (lseek(in, 0, SEEK_SET) < 0) {
        close(in);
        close(target);
        return 1;
    }
    struct stat pending;
    if (!fstatat(target, "Image.pending", &pending, AT_SYMLINK_NOFOLLOW)) {
        if (!S_ISREG(pending.st_mode) || pending.st_uid || pending.st_nlink != 1 ||
            unlinkat(target, "Image.pending", 0)) {
            close(in);
            close(target);
            return 1;
        }
    }
    int out =
        openat(target, "Image.pending", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (out < 0) {
        close(in);
        close(target);
        return 1;
    }
    bad = ftruncate(out, st.st_size) || disk_copy_verify(in, out, (uint64_t)st.st_size) ||
          !kernel_hash_ok(out) || fchmod(out, 0400) || fsync(out);
    close(in);
    close(out);
    if (!bad)
        bad = renameat(target, "Image.pending", target, "Image") || fsync(target);
    close(target);
    return !!bad;
}
static int profile_clone(void) {
    if (kernel_ready())
        return 27;
    int lock = profile_lock();
    if (lock < 0)
        return 28;
    int active = local_socket(0);
    if (active >= 0) {
        close(active);
        return 28;
    }
    if (mkdir(BASE, 0700) && errno != EEXIST)
        return 29;
    struct stat directory;
    if (lstat(BASE, &directory) || !S_ISDIR(directory.st_mode) || directory.st_uid ||
        (directory.st_mode & 077))
        return 29;
    if (disk_path_exists(BASE "/profile-ready"))
        return disk_regular(BASE "/profile-ready") && disk_regular(OVERLAY) ? 0 : 29;
    // Any ambiguous completed disk is preserved for explicit recovery.
    if (disk_path_exists(OVERLAY))
        return 29;
    struct stat source;
    struct statvfs space;
    if (lstat(STOCK_BASE "/debian-v2.qcow2", &source) || !S_ISREG(source.st_mode) ||
        source.st_uid || statvfs(BASE, &space) ||
        (uint64_t)space.f_bavail * space.f_frsize < (uint64_t)source.st_size + 512ULL * 1024 * 1024)
        return 30;
    if (disk_path_exists(OVERLAY ".pending") &&
        (!disk_regular(OVERLAY ".pending") || unlink(OVERLAY ".pending")))
        return 29;
    if (profile_copy(STOCK_BASE "/debian-v2.qcow2", OVERLAY ".pending"))
        return 31;
    // Read-only immutable base may be shared; each profile's writable qcow2 is separate.
    if (!disk_path_exists(BACKING) && link(STOCK_BASE "/base-root.ext4", BACKING))
        return 31;
    struct stat original, linked;
    if (lstat(STOCK_BASE "/base-root.ext4", &original) || lstat(BACKING, &linked) ||
        !S_ISREG(linked.st_mode) || linked.st_uid || (linked.st_mode & 0222) ||
        original.st_ino != linked.st_ino || original.st_dev != linked.st_dev)
        return 31;
    const char *names[] = {"base-v2.identity"};
    for (unsigned i = 0; i < 1; i++) {
        char src[256], dst[256];
        snprintf(src, sizeof(src), "%s/%s", STOCK_BASE, names[i]);
        snprintf(dst, sizeof(dst), "%s/%s", BASE, names[i]);
        if (!disk_path_exists(dst) && profile_copy(src, dst)) {
            if (i == 0)
                return 31;
        }
    }
    struct resource_settings defaults = {512, 1, 30, 0, 1, 1};
    if (resources_save(&defaults) || graphics_save(0, 2, 30))
        return 31;
    if (rename(OVERLAY ".pending", OVERLAY) || disk_dir_sync())
        return 31;
    int fd =
        open(BASE "/profile-ready", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return 31;
    int error = plb_write_all(fd, "PLB_PROFILE_COPY_1\n", 19) || fsync(fd);
    close(fd);
    return error || disk_dir_sync() ? 31 : 0;
}
#endif
