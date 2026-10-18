/* MIT. Recreate larger qcow2 using an offline protected VM, never a host mount. */
#ifndef PLB_DISK_MIGRATION_H
#define PLB_DISK_MIGRATION_H
static int disk_read_exact(int fd, void *bytes, size_t size) {
    unsigned char *p = bytes;
    while (size) {
        ssize_t n = read(fd, p, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        p += n;
        size -= (size_t)n;
    }
    return 0;
}
#include "disk_migration_state.h"
static uint64_t disk_u64be(const unsigned char *p) {
    uint64_t n = 0;
    for (int i = 0; i < 8; i++)
        n = (n << 8) | p[i];
    return n;
}
static uint64_t disk_virtual_size(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    struct stat st;
    unsigned char header[32];
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 &&
             pread(fd, header, sizeof(header), 0) == sizeof(header) &&
             !memcmp(header, "QFI\373", 4);
    close(fd);
    return ok ? disk_u64be(header + 24) : 0;
}
static int disk_target_allowed(int gib) {
    uint64_t current = disk_virtual_size(OVERLAY);
    return !gib || !current || (uint64_t)gib * 1024 * 1024 * 1024 >= current;
}
static int disk_cancel_requests(int control, uid_t module_uid) {
    struct pollfd p = {control, POLLIN, 0};
    if (poll(&p, 1, 0) <= 0)
        return 0;
    int client = accept4(control, NULL, NULL, SOCK_CLOEXEC);
    if (client < 0)
        return 0;
    int uid = peer_uid(client);
    char request = 0;
    struct timeval timeout = {1, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if ((uid == 0 || uid == (int)module_uid) && read(client, &request, 1) == 1) {
        if (request == 'X')
            stopped = 1;
        else if (request == '?')
            plb_write_all(client, READY, sizeof(READY) - 1);
        else
            send_status(client, 12);
    } else
        send_status(client, 12);
    close(client);
    return stopped;
}
static int disk_wait(pid_t child, int control, uid_t module_uid, int seconds) {
    int status = 0;
    double until = plb_now() + seconds;
    pid_t done;
    do {
        done = waitpid(child, &status, WNOHANG);
        if (!done) {
            disk_cancel_requests(control, module_uid);
            if (stopped)
                break;
            usleep(100000);
        }
    } while (!done && plb_now() < until);
    if (!done) {
        kill(child, SIGTERM);
        wait_or_kill(child, 1000);
        return -1;
    }
    return done > 0 && WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
}
static int disk_copy_verified(void) {
    int fd = open(BASE "/migration-console.log", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    struct stat st;
    char data[65537];
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && st.st_size > 0 &&
             st.st_size <= 65536;
    ssize_t n = ok ? read(fd, data, sizeof(data) - 1) : -1;
    close(fd);
    if (n <= 0)
        return 0;
    data[n] = 0;
    return strstr(data, "PLB_DISK_COPY_VERIFIED") && !strstr(data, "PLB_DISK_COPY_FAILED");
}
/* Returns 1 after promotion, 0 for unchanged, -1 without modifying old data. */
static int disk_prepare_growth(int control, uid_t module_uid) {
    if (!vm_resources.disk_gib)
        return 0;
    uint64_t current = disk_virtual_size(OVERLAY),
             target = (uint64_t)vm_resources.disk_gib * 1024 * 1024 * 1024;
    if (!current || target < current)
        return -1;
    if (target == current)
        return 0;
    if (disk_path_exists(BASE "/storage-previous.qcow2"))
        return -1;
    struct statvfs free_space;
    if (statvfs(BASE, &free_space) ||
        (uint64_t)free_space.f_bavail * free_space.f_frsize < target + 512ULL * 1024 * 1024)
        return -1;
    if (disk_remove_pending())
        return -1;
    pid_t parent = getpid(), child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(112);
        log_child(BASE "/migration-create.log");
        char bytes[32];
        snprintf(bytes, sizeof(bytes), "%llu", (unsigned long long)target);
        execl("/apex/com.android.virt/bin/crosvm", "crosvm", "create_qcow2", NEW_DISK, bytes,
              (char *)NULL);
        _exit(113);
    }
    if (child < 0 || disk_wait(child, control, module_uid, 15) ||
        disk_virtual_size(NEW_DISK) != target)
        return -1;
    unlink(BASE "/migration-console.log");
    child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(114);
        log_child(BASE "/migration-vmm.log");
        execl("/apex/com.android.virt/bin/crosvm", "crosvm", "--log-level", "info", "run", "--name",
              "protected-linux-migration", "--disable-sandbox", "--no-balloon",
              "--protected-vm-without-firmware", "--swiotlb", "32", "--mem", "size=512", "--cpus",
              "num-cores=1", "--serial",
              "type=file,path=" BASE "/migration-console.log,console=true,earlycon=true",
              "--params",
              "console=ttyS0 8250.nr_uarts=1 rdinit=/init panic=0 quiet loglevel=3 plb.migrate=1 "
              "plb.offline=1",
              "--block", "path=" OVERLAY ",ro=true", "--block", "path=" NEW_DISK ",ro=false",
              "--initrd", BASE "/debian-initramfs.cpio",
              "/apex/com.android.virt/etc/fs/microdroid_kernel", (char *)NULL);
        _exit(115);
    }
    if (child < 0 || disk_wait(child, control, module_uid, 480) || !disk_copy_verified() ||
        disk_virtual_size(NEW_DISK) != target)
        return -1;
    int fd = open(NEW_DISK, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    int error = fsync(fd);
    close(fd);
    if (error)
        return -1;
    if (disk_path_exists(OLD_DISK))
        return -1;
    fd = open(DISK_TRANSACTION, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    error = plb_write_all(fd, "PLB_DISK_GROWTH_1\n", 18) || fsync(fd);
    close(fd);
    if (error || disk_dir_sync() || rename(OVERLAY, OLD_DISK) || disk_dir_sync() ||
        rename(NEW_DISK, OVERLAY) || disk_dir_sync()) {
        disk_recover();
        return -1;
    }
    return 1;
}
static int disk_confirm_growth(void) {
    int guest = guest_connect(300);
    if (guest < 0)
        return -1;
    unsigned char reply[9];
    int error = plb_write_all(guest, "T", 1) || disk_read_exact(guest, reply, sizeof(reply)) ||
                reply[0] ||
                disk_u64be(reply + 1) != (uint64_t)vm_resources.disk_gib * 1024 * 1024 * 1024;
    close(guest);
    if (error)
        return -1;
    if (!disk_regular(OLD_DISK) || !disk_regular(DISK_TRANSACTION) || unlink(OLD_DISK) ||
        disk_dir_sync() || unlink(DISK_TRANSACTION) || disk_dir_sync())
        return -1;
    return 0;
}
#endif
