/* MIT. One explicitly approved, boot-bound host compaction comparison.
 * The only policy write is one "1\n" to compact_memory. No VM, RAM pressure,
 * task killing, cache drop or persistent tuning. Page placement has no rollback.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "profile_lock.h"
#include "host_memory_health.h"
#include "diagnostic_kernel_monitor.h"
#include "compaction_contract.generated.h"

static long long milliseconds(void) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time))
        return -1;
    return (long long)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static int read_text(const char *path, char *text, size_t capacity) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return 0;
    size_t length = 0;
    while (length < capacity - 1) {
        ssize_t count = read(fd, text + length, capacity - 1 - length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            close(fd);
            return 0;
        }
        if (!count)
            break;
        length += (size_t)count;
    }
    close(fd);
    if (!length || length == capacity - 1)
        return 0;
    text[length] = 0;
    return 1;
}

static int platform(void) {
    char value[512];
    struct utsname kernel;
    if (getuid() || sysconf(_SC_PAGESIZE) != 4096 || uname(&kernel) ||
        strcmp(kernel.release, "6.12.69-android16-6-pee899be-abogkiS948QOPU1BZI9-4k"))
        return 0;
    __system_property_get("ro.product.model", value);
    if (strcmp(value, "SM-S948Q"))
        return 0;
    __system_property_get("ro.build.version.incremental", value);
    if (strcmp(value, "S948QOPU1BZI9"))
        return 0;
    __system_property_get("ro.build.version.sdk", value);
    if (strcmp(value, "37") || !read_text("/sys/fs/selinux/enforce", value, sizeof(value)) ||
        strcmp(value, "1"))
        return 0;
    if (!read_text("/proc/sys/kernel/random/boot_id", value, sizeof(value)) ||
        strcmp(value, PLB_COMPACTION_BOOT_ID "\n"))
        return 0;
    const int sizes[] = {16, 32, 64, 128, 256, 512};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); i++) {
        char path[256];
        snprintf(path, sizeof(path),
                 "/sys/kernel/mm/transparent_hugepage/hugepages-%dkB/shmem_enabled", sizes[i]);
        if (!read_text(path, value, sizeof(value)) || !strstr(value, "[never]"))
            return 0;
    }
    return plb_host_memory_healthy();
}

static int available(void) {
    FILE *input = fopen("/proc/meminfo", "re");
    if (!input)
        return 0;
    unsigned long long free_kib = 0;
    char line[256];
    while (fgets(line, sizeof(line), input))
        if (sscanf(line, "MemAvailable: %llu kB", &free_kib) == 1)
            break;
    fclose(input);
    return free_kib >= 1536ULL * 1024;
}

static int snapshot(const char *phase) {
    const char *names[] = {"buddyinfo", "pagetypeinfo", "meminfo", "vmstat"};
    char *text = malloc(512 * 1024);
    if (!text)
        return 0;
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
        char path[128];
        snprintf(path, sizeof(path), "/proc/%s", names[i]);
        if (!read_text(path, text, 512 * 1024)) {
            free(text);
            return 0;
        }
        printf("PLB_COMPACTION_SNAPSHOT_BEGIN %s %s\n%s", phase, names[i], text);
        printf("PLB_COMPACTION_SNAPSHOT_END %s %s\n", phase, names[i]);
    }
    free(text);
    return 1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2 || strcmp(argv[1], "--once-only") || !platform() || !available())
        return 40;
    int maintenance = profile_maintenance_lock(LOCK_SH), owner = profile_lock();
    if (maintenance < 0 || owner < 0)
        return 41;
    int monitor = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (monitor < 0 || lseek(monitor, 0, SEEK_END) < 0 || !snapshot("before"))
        return 42;
    /* Durable O_EXCL receipt is never removed, even if fork/write fails. It
     * represents consumption of this one approval, not a successful result. */
    int receipt =
        open(PLB_COMPACTION_RECEIPT, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    const char consumed[] = PLB_COMPACTION_BOOT_ID " one attempt consumed\n";
    if (receipt < 0 || write(receipt, consumed, sizeof(consumed) - 1) != sizeof(consumed) - 1 ||
        fsync(receipt))
        return 43;
    close(receipt);
    int directory = open("/data/user/0/net.ryuya_dev.protectedlinux/files",
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0 || fsync(directory))
        return 43;
    close(directory);
    long long started = milliseconds();
    if (started < 0 || !available() || !plb_host_memory_healthy())
        return 44;
    pid_t parent = getpid(), child = fork();
    if (!child) {
        /* Keep inherited owner/maintenance locks if an uninterruptible kernel
         * operation outlives the supervisor. Signal only this known child. */
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(126);
        int fd = open("/proc/sys/vm/compact_memory", O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
            _exit(45);
        errno = 0;
        ssize_t written = write(fd, "1\n", 2);
        int error = errno;
        close(fd);
        printf("PLB_COMPACTION_WRITE bytes=%zd errno=%d elapsed_ms=%lld\n", written, error,
               milliseconds() - started);
        _exit(written == 2 ? 0 : 46);
    }
    if (child < 0)
        return 47;
    int status = 0, reaped = 0, aborted = 0;
    while (milliseconds() - started < 45000) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            reaped = 1;
            break;
        }
        if ((done < 0 && errno != EINTR) || !plb_host_memory_healthy() ||
            plb_diagnostic_kernel_bad_with_traces(monitor, 1) || !available()) {
            aborted = 1;
            break;
        }
        usleep(100000);
    }
    if (!reaped) {
        kill(child, SIGKILL);
        for (int i = 0; i < 20; i++) {
            if (waitpid(child, &status, WNOHANG) == child) {
                reaped = 1;
                break;
            }
            usleep(100000);
        }
        printf("PLB_COMPACTION_STOP requested=1 child_reaped=%d\n", reaped);
        /* No repeated write and no claim that timeout reverses compaction. */
        return 48;
    }
    if (aborted || !WIFEXITED(status) || WEXITSTATUS(status) || !platform() || !available() ||
        plb_diagnostic_kernel_bad_with_traces(monitor, 1) || !snapshot("after"))
        return 49;
    puts("PLB_COMPACTION_SINGLE_WRITE_COMPLETE effectiveness_unqualified=1");
    return 0;
}
