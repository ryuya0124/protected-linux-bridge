/* MIT. Diskless diagnostic only: replace one VMM's guest-RAM backing.
 *
 * Never installed in production launch flags. Never changes the host kernel,
 * Gunyah ioctls, global memory policy or protected VM type. Private anonymous
 * memory is unsuitable for external backends which receive shared-memory FDs;
 * the guard therefore refuses GPU, disks, vsock, network and input devices.
 */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int enabled;
static int prefault;

static int approved_arguments(void) {
    char args[8192];
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ssize_t size = read(fd, args, sizeof(args));
    close(fd);
    if (size <= 0 || size == sizeof(args) || args[size - 1])
        return 0;
    int named = 0, protected = 0, no_balloon = 0, no_sandbox = 0;
    const char *allowed[] = {"--name",       "--disable-sandbox",
                             "--no-balloon", "--protected-vm-without-firmware",
                             "--swiotlb",    "--mem",
                             "--cpus",       "--serial",
                             "--params",     "--initrd",
                             "--hugepages"};
    for (ssize_t at = 0; at < size;) {
        const char *arg = args + at;
        size_t length = strlen(arg);
        if (!strcmp(arg, "--name") && at + length + 1 < (size_t)size &&
            !strcmp(arg + length + 1, "plb-memory-probe"))
            named = 1;
        if (!strcmp(arg, "--protected-vm-without-firmware"))
            protected = 1;
        if (!strcmp(arg, "--no-balloon"))
            no_balloon = 1;
        if (!strcmp(arg, "--disable-sandbox"))
            no_sandbox = 1;
        if (arg[0] == '-') {
            int known = 0;
            for (size_t i = 0; i < sizeof(allowed) / sizeof(*allowed); i++)
                if (!strcmp(arg, allowed[i]))
                    known = 1;
            if (!known)
                return 0;
        }
        at += (ssize_t)length + 1;
    }
    return named && protected && no_balloon && no_sandbox;
}

__attribute__((constructor)) static void initialize(void) {
    const char *mode = getenv("PLB_DISKLESS_BACKING_PROBE");
    struct stat actual, expected;
    if (!mode)
        return;
    if ((strcmp(mode, "anonymous") && strcmp(mode, "anonymous-prefault")) || getuid() ||
        stat("/proc/self/exe", &actual) ||
        stat("/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/crosvm-copy",
             &expected) ||
        actual.st_dev != expected.st_dev || actual.st_ino != expected.st_ino ||
        !approved_arguments())
        _exit(79);
    enabled = 1;
    prefault = !strcmp(mode, "anonymous-prefault");
    /* Do not propagate this experiment through a later exec. */
    unsetenv("LD_PRELOAD");
    unsetenv("PLB_DISKLESS_BACKING_PROBE");
}

static void *map_memory(void *address, size_t size, int prot, int flags, int fd, off_t offset) {
    int replace = 0;
    if (enabled && fd >= 0 && offset == 0 && size >= 128UL * 1024 * 1024 &&
        size <= 1024UL * 1024 * 1024 && (flags & MAP_TYPE) == MAP_SHARED &&
        prot == (PROT_READ | PROT_WRITE)) {
        char path[64], name[128];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
        ssize_t length = readlink(path, name, sizeof(name) - 1);
        if (length > 0 && length < (ssize_t)sizeof(name) - 1) {
            name[length] = 0;
            replace = !strcmp(name, "/memfd:crosvm_guest (deleted)");
        }
    }
    if (replace) {
        flags = (flags & ~MAP_SHARED) | MAP_PRIVATE | MAP_ANONYMOUS;
        fd = -1;
        offset = 0;
    }
    void *mapping = (void *)syscall(SYS_mmap, address, size, prot, flags, fd, offset);
    if (replace && mapping != MAP_FAILED) {
        int advice = madvise(mapping, size, MADV_HUGEPAGE);
        dprintf(STDERR_FILENO, "PLB_BACKING_REPLACED size=%zu hugepage_advice=%d\n", size, advice);
        if (prefault) {
            /* Populate only this new, zero-initialized guest mapping before
             * Gunyah pins/donates its folios. No global THP or compaction setting. */
            volatile unsigned char *bytes = mapping;
            for (size_t at = 0; at < size; at += 4096)
                bytes[at] = 0;
            dprintf(STDERR_FILENO, "PLB_BACKING_PREFAULTED size=%zu alignment=%zu\n", size,
                    (size_t)((uintptr_t)mapping & ((2UL * 1024 * 1024) - 1)));
        }
    }
    return mapping;
}

void *mmap(void *address, size_t size, int prot, int flags, int fd, off_t offset) {
    return map_memory(address, size, prot, flags, fd, offset);
}

void *mmap64(void *address, size_t size, int prot, int flags, int fd, off64_t offset) {
    return map_memory(address, size, prot, flags, fd, (off_t)offset);
}
