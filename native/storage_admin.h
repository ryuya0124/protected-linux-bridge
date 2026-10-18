/* MIT. Fixed-path, root-only disk jobs. No host mounts or arbitrary command API. */
#ifndef PLB_STORAGE_ADMIN_H
#define PLB_STORAGE_ADMIN_H
#include <sys/syscall.h>
#include "storage_state.h"
static int storage_control = -1;
static int storage_socket(int listener) {
    const char name[] = "net.ryuya_dev.protectedlinux.storage";
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    memcpy(a.sun_path + 1, name, sizeof(name) - 1);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    socklen_t size = offsetof(struct sockaddr_un, sun_path) + sizeof(name);
    if (listener ? (bind(fd, (struct sockaddr *)&a, size) || listen(fd, 4))
                 : (connect(fd, (struct sockaddr *)&a, size) || peer_uid(fd) != 0)) {
        close(fd);
        return -1;
    }
    return fd;
}
static int storage_cancel(void) {
    int fd = storage_socket(0);
    if (fd < 0)
        return 1;
    int error = plb_write_all(fd, "X", 1);
    close(fd);
    return !!error;
}
static void storage_poll_cancel(void) {
    if (storage_control < 0)
        return;
    struct pollfd ready = {storage_control, POLLIN, 0};
    if (poll(&ready, 1, 0) <= 0)
        return;
    int fd = accept4(storage_control, NULL, NULL, SOCK_CLOEXEC);
    if (fd < 0)
        return;
    struct timeval timeout = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    char request = 0;
    if (peer_uid(fd) == 0 && read(fd, &request, 1) == 1 && request == 'X')
        stopped = 1;
    close(fd);
}
static void storage_progress(void) {
    static char previous[64];
    int fd = open(BASE "/storage-console.log", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return;
    struct stat st;
    char data[131073];
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_size > 131072) {
        close(fd);
        return;
    }
    ssize_t n = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (n <= 0)
        return;
    data[n] = 0;
    const char *stages[] = {"PLB_STORAGE_COPY_WORK", "PLB_STORAGE_FSCK_WORK", "PLB_STORAGE_RESIZE",
                            "PLB_STORAGE_ZERO_FREE", "PLB_STORAGE_COPY_FINAL"};
    const char *last = NULL;
    for (unsigned i = 0; i < sizeof(stages) / sizeof(*stages); i++)
        if (strstr(data, stages[i]))
            last = stages[i];
    if (last && strcmp(last, previous)) {
        snprintf(previous, sizeof(previous), "%s", last);
        puts(last);
        fflush(stdout);
    }
}
static int storage_wait(pid_t child, int seconds) {
    if (child <= 0)
        return -1;
    int status = 0;
    double deadline = plb_now() + seconds;
    pid_t done;
    do {
        done = waitpid(child, &status, WNOHANG);
        if (!done) {
            storage_poll_cancel();
            storage_progress();
            usleep(100000);
        }
    } while (!done && !stopped && plb_now() < deadline);
    if (!done) {
        wait_or_kill(child, 0);
        return -1;
    }
    return done > 0 && WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
}
static int storage_create(const char *path, uint64_t bytes) {
    pid_t parent = getpid(), child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(1);
        log_child(BASE "/storage-create.log");
        char size[32];
        snprintf(size, sizeof(size), "%llu", (unsigned long long)bytes);
        execl("/apex/com.android.virt/bin/crosvm", "crosvm", "create_qcow2", path, size,
              (char *)NULL);
        _exit(1);
    }
    return storage_wait(child, 15) || disk_virtual_size(path) != bytes ? -1 : 0;
}
static int storage_verified(void) {
    int fd = open(BASE "/storage-console.log", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    struct stat st;
    char data[131073];
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && !st.st_uid && st.st_nlink == 1 &&
             st.st_size > 0 && st.st_size <= 131072;
    ssize_t n = ok ? read(fd, data, sizeof(data) - 1) : -1;
    close(fd);
    if (n <= 0)
        return 0;
    data[n] = 0;
    return strstr(data, "PLB_STORAGE_VERIFIED") && !strstr(data, "PLB_STORAGE_FAILED");
}
static int storage_job(int gib, int compact) {
    if (storage_remove(BASE "/storage-console.log"))
        return 42;
    uint64_t current = disk_virtual_size(OVERLAY), target = (uint64_t)gib * 1024 * 1024 * 1024;
    if (gib < 1 || gib > 128 || !current || current > 128ULL * 1024 * 1024 * 1024 ||
        disk_path_exists(STORAGE_BACKUP))
        return 40;
    uint64_t work = current > target ? current : target;
    struct statvfs space;
    if (statvfs(BASE, &space) || space.f_frsize == 0 ||
        (uint64_t)space.f_bavail * space.f_frsize < work + target + 512ULL * 1024 * 1024)
        return 41;
    if (access(BASE "/debian-initramfs.cpio", R_OK) || storage_create(STORAGE_WORK, work) ||
        storage_create(STORAGE_NEW, target))
        return 42;
    if (storage_remove(BASE "/storage-console.log"))
        return 42;
    printf("STORAGE_JOB working\n");
    fflush(stdout);
    pid_t parent = getpid(), child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(1);
        log_child(BASE "/storage-vmm.log");
        char params[256];
        snprintf(params, sizeof(params),
                 "console=ttyS0 8250.nr_uarts=1 rdinit=/init panic=0 quiet loglevel=3 "
                 "plb.storage=1 plb.offline=1 plb.targetgib=%d plb.compact=%d",
                 gib, compact);
        execl("/apex/com.android.virt/bin/crosvm", "crosvm", "--log-level", "info", "run", "--name",
              "protected-linux-storage", "--disable-sandbox", "--no-balloon",
              "--protected-vm-without-firmware", "--swiotlb", "32", "--mem", "size=512", "--cpus",
              "num-cores=1", "--serial",
              "type=file,path=" BASE "/storage-console.log,console=true,earlycon=true", "--params",
              params, "--block", "path=" OVERLAY ",ro=true", "--block",
              "path=" STORAGE_WORK ",ro=false", "--block", "path=" STORAGE_NEW ",ro=false",
              "--initrd", BASE "/debian-initramfs.cpio",
              "/apex/com.android.virt/etc/fs/microdroid_kernel", (char *)NULL);
        _exit(1);
    }
    if (storage_wait(child, 1800) || !storage_verified() || stopped ||
        disk_virtual_size(STORAGE_NEW) != target)
        return 43;
    if (storage_promote(gib))
        return 44;
    if (storage_remove(STORAGE_WORK) || disk_dir_sync())
        return 45;
    puts("STORAGE_JOB committed");
    return 0;
}
static int storage_admin(const char *action, int gib) {
    umask(077);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGHUP, stop);
    signal(SIGPIPE, SIG_IGN);
    int lock = profile_lock();
    if (lock < 0)
        return 28;
    int active = local_socket(0);
    if (active >= 0) {
        close(active);
        close(lock);
        return 28;
    }
    struct stat dir;
    int error = lstat(BASE, &dir) || !S_ISDIR(dir.st_mode) || dir.st_uid || (dir.st_mode & 077);
    if (error || disk_recover() || storage_recover()) {
        close(lock);
        return 39;
    }
    if (!strcmp(action, "--storage-restore")) {
        if (!disk_regular(STORAGE_BACKUP) || !disk_virtual_size(STORAGE_BACKUP) ||
            !disk_virtual_size(OVERLAY))
            error = 46;
        else {
            struct resource_settings r;
            if (resources_read(&r))
                error = 46;
            else {
                r.disk_gib = 0;
                /* Neutralize a pending growth before the single atomic exchange. Both sides
                 * survive. */
                if (resources_save(&r) || disk_dir_sync())
                    error = 46;
                else if (syscall(SYS_renameat2, AT_FDCWD, OVERLAY, AT_FDCWD, STORAGE_BACKUP, 2) ||
                         disk_dir_sync())
                    error = 46;
            }
        }
    } else if (!strcmp(action, "--storage-discard"))
        error = !disk_virtual_size(OVERLAY) || storage_remove(STORAGE_BACKUP) || disk_dir_sync()
                    ? 46
                    : 0;
    else {
        if (!strcmp(action, "--storage-compact")) {
            uint64_t bytes = disk_virtual_size(OVERLAY);
            if (bytes % (1024ULL * 1024 * 1024)) {
                close(lock);
                return 40;
            }
            gib = (int)(bytes / (1024ULL * 1024 * 1024));
        }
        storage_control = storage_socket(1);
        if (storage_control < 0) {
            close(lock);
            return 28;
        }
        error = storage_job(gib, 1);
        close(storage_control);
        storage_control = -1;
        /* Roll back a failed/interrupted promotion, never overwrite the original on failure. */
        if (error && storage_recover())
            error = 47;
    }
    close(lock);
    return error;
}
static uint64_t storage_allocated(const char *path) {
    struct stat s;
    return !lstat(path, &s) && S_ISREG(s.st_mode) && !s.st_uid ? (uint64_t)s.st_blocks * 512 : 0;
}
static int storage_state(void) {
    struct statvfs space;
    uint64_t free_bytes = statvfs(BASE, &space) ? 0 : (uint64_t)space.f_bavail * space.f_frsize;
    uint64_t total = 0, used = 0, available = 0;
    int active = 0;
    int fd = local_socket(0);
    if (fd >= 0) {
        struct timeval t = {3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
        unsigned char reply[26];
        if (!plb_write_all(fd, "S", 1) && !disk_read_exact(fd, reply, sizeof(reply)) &&
            reply[0] <= 1 && !reply[1]) {
            active = reply[0];
            total = disk_u64be(reply + 2);
            used = disk_u64be(reply + 10);
            available = disk_u64be(reply + 18);
        }
        close(fd);
    }
    printf("STORAGE %llu %llu %llu %llu %llu %llu %llu %llu %d\n",
           (unsigned long long)disk_virtual_size(OVERLAY),
           (unsigned long long)storage_allocated(OVERLAY),
           (unsigned long long)disk_virtual_size(STORAGE_BACKUP),
           (unsigned long long)storage_allocated(STORAGE_BACKUP), (unsigned long long)free_bytes,
           (unsigned long long)total, (unsigned long long)used, (unsigned long long)available,
           active);
    return 0;
}
#endif
