/* MIT. One disk-isolated native UEFI trial; no Android settings or host modules. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../native/host_android17.h"
#include "../host_memory_health.h"
#include "../diagnostic_kernel_monitor.h"
#include "../diagnostic_owner_lock.h"
#include "../probe_watchdog.h"
#include "runner_contract.generated.h"

#ifndef PLB_UEFI_VM_NAME
#define PLB_UEFI_VM_NAME "plb-native-uefi"
#endif
#ifndef PLB_UEFI_RESULT_KIND
#define PLB_UEFI_RESULT_KIND "UEFI"
#endif

static volatile sig_atomic_t stopping;
static void request_stop(int number) {
    (void)number;
    stopping = 1;
}

static int read_file(const char *path, char *buffer, size_t capacity) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        printf("PLB_NATIVE_GUARD read_open_errno=%d path=%s\n", errno, path);
        return 0;
    }
    ssize_t count = read(fd, buffer, capacity - 1);
    close(fd);
    if (count <= 0 || count == (ssize_t)capacity - 1) {
        printf("PLB_NATIVE_GUARD read_count=%lld path=%s\n", (long long)count, path);
        return 0;
    }
    buffer[count] = 0;
    return 1;
}

/* Keep the original strict classification. Record the precise stop instead of
 * treating later healthy postflight as proof the earlier condition was false. */
static int kernel_monitor(int fd) {
    char record[8192];
    for (unsigned index = 0; index < 256; index++) {
        ssize_t count = read(fd, record, sizeof(record) - 1);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        if (count <= 0) {
            printf("PLB_NATIVE_GUARD monitor_read_errno=%d count=%lld\n", errno, (long long)count);
            return 1;
        }
        record[count] = 0;
        if (plb_diagnostic_kernel_bad_text(record, 0)) {
            printf("PLB_NATIVE_GUARD monitor_record=%s\n", record);
            return 1;
        }
    }
    printf("PLB_NATIVE_GUARD monitor_budget_exhausted=256\n");
    return 1;
}

static int same_host(void) {
    char text[4096], path[128];
    if (!read_file("/proc/sys/kernel/random/boot_id", text, sizeof(text)) ||
        strcmp(text, PLB_UEFI_BOOT_ID "\n") || !plb_host_memory_healthy())
        return 0;
    for (size_t index = 0; index < 3; index++) {
        snprintf(path, sizeof(path), "/proc/%d/stat", plb_uefi_core_pids[index]);
        if (!read_file(path, text, sizeof(text)))
            return 0;
        char *position = strrchr(text, ')');
        if (!position || position[1] != ' ')
            return 0;
        position += 2;
        for (int field = 3; field < 22; field++) {
            position = strchr(position, ' ');
            if (!position)
                return 0;
            position++;
        }
        char *end;
        errno = 0;
        unsigned long long ticks = strtoull(position, &end, 10);
        if (errno || end == position || *end != ' ' || ticks != plb_uefi_core_starts[index])
            return 0;
    }
    return 1;
}

static int memory_floor(unsigned additional_mib) {
    char text[8192];
    if (!read_file("/proc/meminfo", text, sizeof(text)))
        return 0;
    char *line = strstr(text, "MemAvailable:");
    unsigned long long available = 0;
    return line && sscanf(line, "MemAvailable: %llu kB", &available) == 1 &&
           available >= (1536ULL + additional_mib) * 1024;
}

static int owned_regular(const char *path) {
    struct stat value;
    return !lstat(path, &value) && S_ISREG(value.st_mode) && value.st_uid == 0 &&
           value.st_nlink == 1 && !(value.st_mode & 077);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getuid() || argc != 2 || strcmp(argv[1], "--run"))
        return 40;
    if (!host17_admitted()) {
        printf("PLB_NATIVE_GUARD phase=admission reason=host_artifact_or_platform\n");
        return 79;
    }
    if (!same_host()) {
        printf("PLB_NATIVE_GUARD phase=admission reason=host_identity_or_health\n");
        return 79;
    }
    if (!memory_floor(1024)) {
        printf("PLB_NATIVE_GUARD phase=admission reason=memory_floor\n");
        return 79;
    }
    if (!owned_regular(PLB_UEFI_BASE "/firmware.fd") ||
        !owned_regular(PLB_UEFI_BASE "/synthetic.raw") ||
        !host17_digest(PLB_UEFI_BASE "/firmware.fd", PLB_UEFI_FIRMWARE_SHA256) ||
        !host17_digest(PLB_UEFI_BASE "/synthetic.raw", PLB_UEFI_DISK_SHA256))
        return 80;
#if defined(PLB_UEFI_PCI_PROBE) && PLB_UEFI_PCI_PROBE
    if (!owned_regular(PLB_UEFI_BASE "/pattern.raw") ||
        !host17_digest(PLB_UEFI_BASE "/pattern.raw", PLB_UEFI_PATTERN_SHA256)) {
        return 80;
    }
#endif
    int maintenance = profile_maintenance_lock(LOCK_SH);
    int owner = diagnostic_owner_lock();
    if (maintenance < 0 || owner < 0)
        return 28;
    int monitor = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (monitor < 0 || lseek(monitor, 0, SEEK_END) < 0)
        return 81;
    int spent = open(PLB_UEFI_BASE "/spent", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (spent < 0 || write(spent, PLB_UEFI_NONCE "\n", 33) != 33 || fsync(spent))
        return 82;
    close(spent);
    int directory = open(PLB_UEFI_BASE, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0 || fsync(directory))
        return 82;
    close(directory);
    struct rlimit limit = {RLIM_INFINITY, RLIM_INFINITY};
    if (setrlimit(RLIMIT_MEMLOCK, &limit))
        return 83;
    signal(SIGTERM, request_stop);
    signal(SIGINT, request_stop);
    signal(SIGHUP, request_stop);
    signal(SIGPIPE, SIG_IGN);
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent || fcntl(owner, F_SETFD, 0) ||
            fcntl(maintenance, F_SETFD, 0))
            _exit(84);
        int log = open(PLB_UEFI_BASE "/vmm.log", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        int console =
            open(PLB_UEFI_BASE "/console.log", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (log < 0 || console < 0 || dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0)
            _exit(85);
        close(log);
        close(console);
        execl("/apex/com.android.virt/bin/crosvm", "crosvm", "--log-level", "info", "run", "--name",
              PLB_UEFI_VM_NAME, "--disable-sandbox", "--hypervisor", "gunyah",
              "--protected-vm-without-firmware", "--mem", "512", "--cpus", "num-cores=1",
              "--swiotlb", "32", "--no-balloon", "--no-usb", "--no-rng", "--no-pmu", "--serial",
              "type=file,path=" PLB_UEFI_BASE "/console.log,console=true,earlycon=true", "--block",
              "path=" PLB_UEFI_BASE "/synthetic.raw,ro=true,lock=false", "--dump-device-tree-blob",
              PLB_UEFI_BASE "/guest-generated.dtb",
#if defined(PLB_UEFI_PCI_PROBE) && PLB_UEFI_PCI_PROBE
              "--block", "path=" PLB_UEFI_BASE "/pattern.raw,ro=true,lock=false",
#endif
              PLB_UEFI_BASE "/firmware.fd", (char *)NULL);
        _exit(86);
    }
    if (child < 0)
        return 87;
    int status = 0, result = 124;
    long long deadline = plb_probe_millis() + 45000;
    while (!stopping && plb_probe_millis() < deadline) {
        if (!same_host()) {
            printf("PLB_NATIVE_GUARD reason=host_identity_or_health\n");
            result = 88;
            break;
        }
        if (!memory_floor(0)) {
            printf("PLB_NATIVE_GUARD reason=memory_floor\n");
            result = 88;
            break;
        }
        if (kernel_monitor(monitor)) {
            printf("PLB_NATIVE_GUARD reason=kernel_monitor\n");
            result = 88;
            break;
        }
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            child = -1;
            break;
        }
        if (done < 0 && errno != EINTR) {
            result = 89;
            break;
        }
        usleep(50000);
    }
    if (child > 0) {
        kill(child, SIGTERM);
        (void)plb_probe_wait(child, 1500);
    }
    if (!same_host() || kernel_monitor(monitor)) {
        printf("PLB_NATIVE_GUARD reason=postflight_identity_health_or_monitor\n");
        result = 88;
    }
    close(monitor);
    close(owner);
    close(maintenance);
    printf("PLB_NATIVE_" PLB_UEFI_RESULT_KIND "_RUNNER nonce=%s result=%d\n", PLB_UEFI_NONCE,
           result);
    return result;
}
