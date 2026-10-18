/* MIT. Offline virtual-block copy. The original disk is opened read-only. */
#ifndef PLB_GUEST_MIGRATION_H
#define PLB_GUEST_MIGRATION_H
#include <stdint.h>
#include <sys/sysmacros.h>
#include "disk_copy.h"
static void guest_migrate_disk(void) {
    int source = -1, target = -1, error = 1;
    FILE *dev = fopen("/sys/block/vdb/dev", "re");
    unsigned major = 0, minor = 0;
    if (dev) {
        if (fscanf(dev, "%u:%u", &major, &minor) != 2)
            major = 0;
        fclose(dev);
    }
    if (major && !mknod("/dev/vdb", S_IFBLK | 0600, makedev(major, minor))) {
        source = open("/dev/vda", O_RDONLY | O_CLOEXEC);
        target = open("/dev/vdb", O_RDWR | O_CLOEXEC);
        uint64_t from = 0, to = 0;
        if (source >= 0 && target >= 0 && !ioctl(source, 0x80081272UL, &from) &&
            !ioctl(target, 0x80081272UL, &to) && from >= 128ULL * 1024 * 1024 && from < to &&
            to <= 128ULL * 1024 * 1024 * 1024) {
            puts("PLB_DISK_COPY_STARTED");
            fflush(stdout);
            error = disk_copy_verify(source, target, from);
        }
    }
    if (target >= 0)
        close(target);
    if (source >= 0)
        close(source);
    sync();
    puts(error ? "PLB_DISK_COPY_FAILED" : "PLB_DISK_COPY_VERIFIED");
    fflush(stdout);
    reboot(RB_POWER_OFF);
    for (;;)
        pause();
}
static uint32_t disk_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t guest_fs_bytes(void) {
    int fd = open("/dev/vda", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    unsigned char super[1024];
    ssize_t n = pread(fd, super, sizeof(super), 1024);
    close(fd);
    if (n != sizeof(super) || super[56] != 0x53 || super[57] != 0xef || disk_le32(super + 24) > 6)
        return 0;
    uint64_t count = disk_le32(super + 4);
    if (disk_le32(super + 96) & 0x80)
        count |= (uint64_t)disk_le32(super + 0x150) << 32;
    return count * (1024ULL << disk_le32(super + 24));
}
static void guest_grow_filesystem(void) {
    int gib = guest_boot_option(" plb.diskgib=");
    if (!gib || guest_fs_bytes() == (uint64_t)gib * 1024 * 1024 * 1024)
        return;
    pid_t child = fork();
    if (!child) {
        execl("/sbin/resize2fs", "resize2fs", "/dev/vda", (char *)NULL);
        execl("/usr/sbin/resize2fs", "resize2fs", "/dev/vda", (char *)NULL);
        _exit(127);
    }
    if (child < 0)
        return;
    int status;
    double until = plb_now() + 90;
    pid_t done;
    do {
        done = waitpid(child, &status, WNOHANG);
        if (!done)
            usleep(100000);
    } while (!done && plb_now() < until);
    if (!done) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    sync();
    if (done <= 0 || !WIFEXITED(status) || WEXITSTATUS(status))
        fputs("PLB_DISK_GROW_FAILED\n", stderr);
}
static void guest_storage_status(int client) {
    unsigned char reply[9] = {0};
    uint64_t size = guest_fs_bytes(),
             wanted = (uint64_t)guest_boot_option(" plb.diskgib=") * 1024 * 1024 * 1024;
    if (!size || (wanted && size != wanted))
        reply[0] = 1;
    for (int i = 0; i < 8; i++)
        reply[i + 1] = (unsigned char)(size >> (56 - i * 8));
    plb_write_all(client, reply, sizeof(reply));
}
#endif
