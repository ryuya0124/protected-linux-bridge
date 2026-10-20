/* MIT. Bounded CPU-only protected VM. Optional read-only Debian input; no GPU or host policy. */
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
#include "../native/host_android17.h"
#include "diagnostic_owner_lock.h"
#include "diagnostic_kernel_monitor.h"
#include "host_memory_health.h"
#include "probe_watchdog.h"
#include "cpu_probe.generated.h"
#include "cpu_probe_result.h"

#ifndef PLB_CPU_KERNEL
#define PLB_CPU_KERNEL "/apex/com.android.virt/etc/fs/microdroid_kernel"
#define PLB_CPU_KERNEL_SHA256 "02f0044263e79ae95f321325ca1741d092f0c78c693b8114830d6e1ff2849aae"
#endif
#ifndef PLB_CPU_EXECUTABLE
#define PLB_CPU_EXECUTABLE "/apex/com.android.virt/bin/crosvm"
#endif
#ifndef PLB_CPU_TIMEOUT_SECONDS
#define PLB_CPU_TIMEOUT_SECONDS 30
#endif

static volatile sig_atomic_t stopping;
static void stop(int signal_number) {
    (void)signal_number;
    stopping = 1;
}

static int enough_memory(unsigned allocation_mib) {
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file)
        return 0;
    char line[256];
    unsigned long long available = 0;
    while (fgets(line, sizeof(line), file))
        if (sscanf(line, "MemAvailable: %llu kB", &available) == 1)
            break;
    fclose(file);
    return available >= (1536ULL + allocation_mib) * 1024;
}

int main(int argc, char **argv) {
    if (getuid() || argc != 2 || strcmp(argv[1], "--run"))
        return 40;
    if (!host17_admitted() || !plb_host_memory_healthy() || !enough_memory(PLB_CPU_RAM_MIB))
        return 79;
    if (!host17_digest(PLB_CPU_BASE "/initramfs.cpio", PLB_CPU_INITRAMFS_SHA256))
        return 80;
    if (!host17_digest(PLB_CPU_KERNEL, PLB_CPU_KERNEL_SHA256))
        return 80;
#ifdef PLB_CPU_EXECUTABLE_SHA256
    if (!host17_digest(PLB_CPU_EXECUTABLE, PLB_CPU_EXECUTABLE_SHA256))
        return 80;
#endif
    int owner = diagnostic_owner_lock();
    if (owner < 0)
        return 28;
    int monitor = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (monitor < 0 || lseek(monitor, 0, SEEK_END) < 0)
        return 81;
    int spent = open(PLB_CPU_BASE "/spent", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (spent < 0)
        return 82;
    close(spent);
    struct rlimit limit = {RLIM_INFINITY, RLIM_INFINITY};
    if (setrlimit(RLIMIT_MEMLOCK, &limit))
        return 83;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGHUP, stop);
    signal(SIGPIPE, SIG_IGN);
    pid_t parent = getpid(), child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(84);
#ifdef PLB_CPU_SHMEM_ADVISE
        char owner_descriptor[32];
        snprintf(owner_descriptor, sizeof(owner_descriptor), "%d", owner);
        if (fcntl(owner, F_SETFD, 0) || setenv(PLB_DIAGNOSTIC_OWNER_ENV, owner_descriptor, 1))
            _exit(84);
#endif
        int log = open(PLB_CPU_BASE "/vmm.log", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (log < 0)
            _exit(85);
        dup2(log, STDOUT_FILENO);
        dup2(log, STDERR_FILENO);
        close(log);
        execl(PLB_CPU_EXECUTABLE, "crosvm", "--log-level", "info", "run", "--name",
              "plb17-cpu-probe", "--disable-sandbox", "--no-balloon", "--hypervisor", "gunyah",
              "--protected-vm-without-firmware", "--swiotlb", "32", "--mem", PLB_CPU_RAM_ARGUMENT,
              "--cpus", "num-cores=1", "--serial",
              "type=file,path=" PLB_CPU_BASE "/console.log,console=true,earlycon=true", "--params",
              "console=ttyS0 earlycon 8250.nr_uarts=1 rdinit=/init panic=0 "
              "plb.cpu_probe=" PLB_CPU_NONCE,
#ifdef PLB_CPU_DEBIAN_READONLY
              "--block",
              "path=/data/user/0/com.android.virtualization.terminal/files/linux/root_part,ro=true",
#endif
              "--initrd", PLB_CPU_BASE "/initramfs.cpio", PLB_CPU_KERNEL, (char *)NULL);
        _exit(86);
    }
    if (child < 0)
        return 87;
    /* Only this unreaped direct child is ever signalled. It cannot be another VM. */
    int status = 0, result = 124;
    long long deadline = plb_probe_millis() + PLB_CPU_TIMEOUT_SECONDS * 1000LL;
    while (!stopping && plb_probe_millis() < deadline) {
        if (!plb_host_memory_healthy() || plb_diagnostic_kernel_bad(monitor) || !enough_memory(0)) {
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
        usleep(25000);
    }
    if (child > 0) {
        kill(child, SIGTERM);
        (void)plb_probe_wait(child, 1000);
    }
    if (!plb_host_memory_healthy() || plb_diagnostic_kernel_bad(monitor))
        result = 88;
    if (!result) {
        int fd = open(PLB_CPU_BASE "/console.log", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        FILE *console = fd >= 0 ? fdopen(fd, "r") : NULL;
#ifdef PLB_CPU_DEBIAN_READONLY
        const int debian_required = 1;
#else
        const int debian_required = 0;
#endif
        if (!plb_cpu_console_success_for_bytes(console, PLB_CPU_NONCE, debian_required,
                                               PLB_CPU_WORKLOAD_BYTES))
            result = 90;
        if (console)
            fclose(console);
        else if (fd >= 0)
            close(fd);
    }
    close(monitor);
    close(owner);
    printf("PLB_CPU_PROBE_EXIT nonce=%s result=%d\n", PLB_CPU_NONCE, result);
    return result;
}
