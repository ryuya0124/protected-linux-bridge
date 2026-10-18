/* MIT. Offline maintenance, exclusively inside an owned protected guest. */
#ifndef PLB_GUEST_STORAGE_H
#define PLB_GUEST_STORAGE_H
#include <sys/mount.h>
#include <sys/statvfs.h>
static uint64_t storage_fs_size(int fd) {
    unsigned char s[1024];
    if (pread(fd, s, sizeof(s), 1024) != sizeof(s) || s[56] != 0x53 || s[57] != 0xef ||
        disk_le32(s + 24) > 6)
        return 0;
    uint64_t n = disk_le32(s + 4);
    if (disk_le32(s + 96) & 0x80)
        n |= (uint64_t)disk_le32(s + 0x150) << 32;
    return n * (1024ULL << disk_le32(s + 24));
}
static int storage_guest_node(const char *name) {
    char sys[64], path[64];
    snprintf(sys, sizeof(sys), "/sys/block/%s/dev", name);
    snprintf(path, sizeof(path), "/dev/%s", name);
    FILE *f = fopen(sys, "re");
    unsigned major = 0, minor = 0;
    if (!f)
        return -1;
    int ok = fscanf(f, "%u:%u", &major, &minor) == 2 && major;
    fclose(f);
    return ok ? mknod(path, S_IFBLK | 0600, makedev(major, minor)) : -1;
}
/* Tools and libraries come from the readonly original, never from Android. */
static int storage_tool(const char *tool, const char *arg, const char *device, const char *size,
                        int fsck) {
    pid_t child = fork();
    if (!child) {
        if (chroot("/newroot") || chdir("/"))
            _exit(125);
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("LC_ALL", "C", 1);
        alarm(480);
        if (!strcmp(tool, "zerofree")) {
            const char *argv[] = {"zerofree", arg, device, NULL};
            execv("/run/plb-zerofree", (char *const *)argv);
            _exit(127);
        }
        char path[128];
        snprintf(path, sizeof(path), "/usr/sbin/%s", tool);
        const char *argv[] = {tool, arg, device, size, NULL};
        if (!size)
            argv[3] = NULL;
        execv(path, (char *const *)argv);
        snprintf(path, sizeof(path), "/sbin/%s", tool);
        execv(path, (char *const *)argv);
        _exit(127);
    }
    if (child < 0)
        return -1;
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return WIFEXITED(status) && (WEXITSTATUS(status) == 0 || (fsck && WEXITSTATUS(status) == 1))
               ? 0
               : -1;
}
static int storage_tools_mount(void) {
    if (mount("/dev/vda", "/newroot", "ext4", MS_RDONLY, "noload"))
        return -1;
    if (mount("tmpfs", "/newroot/dev", "tmpfs", MS_NOSUID, "mode=755,size=4m") ||
        mount("tmpfs", "/newroot/run", "tmpfs", MS_NOSUID, "mode=755,size=4m") ||
        mount("tmpfs", "/newroot/tmp", "tmpfs", MS_NOSUID, "mode=1777,size=8m") ||
        mount("proc", "/newroot/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL))
        return -1;
    const char *nodes[] = {"vdb", "vdc"};
    for (int i = 0; i < 2; i++) {
        char old[32], dest[48];
        struct stat st;
        snprintf(old, sizeof(old), "/dev/%s", nodes[i]);
        snprintf(dest, sizeof(dest), "/newroot/dev/%s", nodes[i]);
        if (stat(old, &st) || mknod(dest, S_IFBLK | 0600, st.st_rdev))
            return -1;
    }
    if (mknod("/newroot/dev/null", S_IFCHR | 0666, makedev(1, 3)) ||
        mknod("/newroot/dev/urandom", S_IFCHR | 0666, makedev(1, 9)))
        return -1;
    int in = open("/plb-zerofree", O_RDONLY | O_CLOEXEC),
        out = open("/newroot/run/plb-zerofree", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700);
    if (in < 0 || out < 0) {
        if (in >= 0)
            close(in);
        if (out >= 0)
            close(out);
        return -1;
    }
    unsigned char buf[4096];
    ssize_t n;
    int error = 0;
    while ((n = read(in, buf, sizeof(buf))) > 0)
        if (plb_write_all(out, buf, (size_t)n)) {
            error = 1;
            break;
        }
    if (n < 0)
        error = 1;
    close(in);
    close(out);
    return error ? -1 : 0;
}
static void storage_guest_offline(void) {
    int error = 1, source = -1, work = -1, dest = -1;
    uint64_t from = 0, work_size = 0, to = 0;
    int gib = guest_boot_option(" plb.targetgib=");
    if (gib < 1 || gib > 128)
        goto finish;
    if (storage_guest_node("vdb") || storage_guest_node("vdc"))
        goto finish;
    source = open("/dev/vda", O_RDONLY | O_CLOEXEC);
    work = open("/dev/vdb", O_RDWR | O_CLOEXEC);
    dest = open("/dev/vdc", O_RDWR | O_CLOEXEC);
    int readonly = 0;
    if (source < 0 || work < 0 || dest < 0 || ioctl(source, 0x125e, &readonly) || !readonly ||
        ioctl(source, 0x80081272UL, &from) || ioctl(work, 0x80081272UL, &work_size) ||
        ioctl(dest, 0x80081272UL, &to) || to != (uint64_t)gib * 1024 * 1024 * 1024 ||
        work_size < (from > to ? from : to))
        goto finish;
    puts("PLB_STORAGE_COPY_WORK");
    fflush(stdout);
    if (disk_copy_verify(source, work, from) || storage_tools_mount())
        goto finish;
    puts("PLB_STORAGE_FSCK_WORK");
    fflush(stdout);
    if (storage_tool("e2fsck", "-fp", "/dev/vdb", NULL, 1))
        goto finish;
    uint64_t fs = storage_fs_size(work);
    if (!fs || fs > from)
        goto finish;
    if (fs != to) {
        char size[32];
        snprintf(size, sizeof(size), "%lluK", (unsigned long long)(to / 1024));
        puts("PLB_STORAGE_RESIZE");
        fflush(stdout);
        if (storage_tool("resize2fs", "-p", "/dev/vdb", size, 0) || storage_fs_size(work) != to ||
            storage_tool("e2fsck", "-fp", "/dev/vdb", NULL, 1))
            goto finish;
    }
    if (guest_boot_option(" plb.compact=")) {
        puts("PLB_STORAGE_ZERO_FREE");
        fflush(stdout);
        if (storage_tool("zerofree", "-v", "/dev/vdb", NULL, 0))
            goto finish;
    }
    puts("PLB_STORAGE_COPY_FINAL");
    fflush(stdout);
    if (fsync(work) || disk_copy_verify(work, dest, to) || storage_fs_size(dest) != to ||
        storage_tool("e2fsck", "-fn", "/dev/vdc", NULL, 0))
        goto finish;
    error = 0;
finish:
    if (dest >= 0)
        close(dest);
    if (work >= 0)
        close(work);
    if (source >= 0)
        close(source);
    sync();
    puts(error ? "PLB_STORAGE_FAILED" : "PLB_STORAGE_VERIFIED");
    fflush(stdout);
    reboot(RB_POWER_OFF);
    for (;;)
        pause();
}
static void guest_storage_usage(int client) {
    struct statvfs st;
    unsigned char reply[25] = {0};
    if (statvfs("/", &st)) {
        reply[0] = 1;
        plb_write_all(client, reply, sizeof(reply));
        return;
    }
    uint64_t v[] = {st.f_blocks * (uint64_t)st.f_frsize,
                    (st.f_blocks - st.f_bfree) * (uint64_t)st.f_frsize,
                    st.f_bavail * (uint64_t)st.f_frsize};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 8; i++)
            reply[1 + j * 8 + i] = (unsigned char)(v[j] >> (56 - 8 * i));
    plb_write_all(client, reply, sizeof(reply));
}
#endif
