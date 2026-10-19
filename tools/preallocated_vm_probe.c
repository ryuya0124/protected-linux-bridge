/* MIT. Private VMM with a diskless guest or isolated Android copy; RAM is adopted, never copied.
 * No policy write, mapped discard, global compaction, production disk or module.
 * Compile pins are generated per build; an owner-lock parent supervises only its
 * direct child and watches new kernel reports. No automatic VMM retry exists.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/memfd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "diagnostic_owner_lock.h"
#include "host_memory_health.h"
#include "preallocated_vm_limits.h"
#include "preallocated_pin.h"
#include "diagnostic_kernel_monitor.h"
#include "preallocated_collapse.h"
#include "owned_page_summary.h"
#include "preallocated_ready.h"
#include "preallocated_vm.generated.h"

#if defined(PLB_ANDROID4G_CUSTOM_GUEST) && !defined(PLB_PREALLOCATED_ANDROID_PROXY)
#error "Custom guest kernel requires the isolated adopted-RAM Android proxy"
#endif
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#ifdef PLB_PREALLOCATED_ANDROID_PROXY
#include "android4g_config.h"
#define VMM PLB_ANDROID4G_VMM
#define GUEST_KERNEL PLB_ANDROID4G_KERNEL
#define INITRAMFS PLB_ANDROID4G_BASE "/android-lowram.cpio"
#include "android4g_arguments.h"
#else
#define VMM FILES "/kernel-probes/preallocated-vmm"
#define GUEST_KERNEL FILES "/kernels/linux-6.18.57-plb1/Image"
#ifdef PLB_PREALLOCATED_FULL_CONTENT
#define INITRAMFS FILES "/kernel-probes/memory-full-probe.cpio"
#else
#define INITRAMFS FILES "/kernel-probes/memory-probe.cpio"
#endif
#endif
#define MIB (1024UL * 1024)

#ifndef PLB_PREALLOCATED_ANDROID_PROXY
static volatile sig_atomic_t stopping;
#endif

static long long now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

#ifndef PLB_PREALLOCATED_ANDROID_PROXY
static void stop(int signal) {
    (void)signal;
    stopping = 1;
}
#endif

static int private_file(const char *path) {
    struct stat info;
    return !lstat(path, &info) && S_ISREG(info.st_mode) && info.st_uid == 0 && info.st_nlink == 1 &&
           !(info.st_mode & 077);
}

static int pinned_file(const char *path, const char *digest) {
    if (!private_file(path)) {
        return 0;
    }
    int descriptors[2];
    if (pipe2(descriptors, O_CLOEXEC)) {
        return 0;
    }
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) {
            _exit(126);
        }
        dup2(descriptors[1], STDOUT_FILENO);
        close(descriptors[0]);
        close(descriptors[1]);
        execl("/system/bin/toybox", "toybox", "sha256sum", path, (char *)NULL);
        _exit(126);
    }
    close(descriptors[1]);
    char text[256] = {0};
    size_t used = 0;
    while (used < sizeof(text)) {
        ssize_t size = read(descriptors[0], text + used, sizeof(text) - used);
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size <= 0) {
            break;
        }
        used += (size_t)size;
    }
    close(descriptors[0]);
    int status = 0;
    if (child <= 0) {
        return 0;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return 0;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && used > 65 && text[64] == ' ' &&
           !memcmp(text, digest, 64);
}

static int available(int memory) {
    FILE *input = fopen("/proc/meminfo", "re");
    if (!input) {
        return 0;
    }
    char line[256];
    unsigned long kib = 0;
    while (fgets(line, sizeof(line), input)) {
        if (sscanf(line, "MemAvailable: %lu kB", &kib) == 1) {
            break;
        }
    }
    fclose(input);
    return kib >= (1536UL + (unsigned long)memory) * 1024;
}

static int platform(void) {
    struct utsname kernel;
    char value[PROP_VALUE_MAX];
    __system_property_get("ro.product.model", value);
    if (geteuid() || strcmp(value, "SM-S948Q") || uname(&kernel) ||
        strcmp(kernel.release, "6.12.30-android16-5-pd30ff70-abogkiS948QOPS1AZHL-4k")) {
        return 0;
    }
    __system_property_get("ro.build.version.incremental", value);
    if (strcmp(value, "S948QOPS1AZHL") || sysconf(_SC_PAGESIZE) != 4096) {
        return 0;
    }
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    char enforcing = 0;
    int okay = fd >= 0 && read(fd, &enforcing, 1) == 1 && enforcing == '1';
    if (fd >= 0) {
        close(fd);
    }
    return okay;
}

static int observe_backing(const void *mapping, size_t bytes) {
    int pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    int flags_fd = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    if (pagemap < 0 || flags_fd < 0) {
        return 0;
    }
    size_t pages = 0, thp = 0, heads = 0;
    struct plb_page_summary summary = {0};
    for (size_t at = 0; at < bytes; at += 4096) {
        uint64_t entry, flags;
        off_t index = (off_t)(((uintptr_t)mapping + at) / 4096 * 8);
        if (pread(pagemap, &entry, 8, index) != 8 || !(entry & (UINT64_C(1) << 63))) {
            break;
        }
        uint64_t pfn = entry & ((UINT64_C(1) << 55) - 1);
        if (!pfn || pread(flags_fd, &flags, 8, (off_t)(pfn * 8)) != 8) {
            break;
        }
        pages++;
        thp += !!(flags & (UINT64_C(1) << 22));
        heads += !!(flags & (UINT64_C(1) << 15));
        plb_page_observe(&summary, 1, 1, pfn, 1, flags);
    }
    close(pagemap);
    close(flags_fd);
    printf("PLB_PREALLOCATED_PAGES sampled=%zu thp=%zu compound_heads=%zu expected=%zu\n", pages,
           thp, heads, bytes / 4096);
    if ((uintptr_t)mapping % PLB_COLLAPSE_BYTES == 0 && bytes % PLB_COLLAPSE_BYTES == 0) {
        printf("PLB_PREALLOCATED_2M folios=%zu windows=%zu\n", summary.sampled_2m_folios,
               bytes / PLB_COLLAPSE_BYTES);
    }
    printf("PLB_PREALLOCATED_ALIGNMENT virtual_2m=%d file_batch_2m=%d\n",
           (uintptr_t)mapping % PLB_COLLAPSE_BYTES == 0, bytes % PLB_COLLAPSE_BYTES == 0);
    return pages == bytes / 4096;
}

static int verify_after_pin(volatile unsigned char *mapping, size_t bytes) {
    /* Migration can invalidate CPU PTEs without losing the owned file's data.
     * Resolve them through normal access and verify zero-fill before sampling.
     * No fallocate, MADV_REMOVE/DONTNEED, truncation or mapped-hole operation. */
    for (size_t at = 0; at < bytes; at += 4096) {
        if (mapping[at] != 0) {
            return 0;
        }
        mapping[at] = 0;
        if (!(at % (64UL * MIB)) && (!plb_host_memory_healthy() || !available(0))) {
            return 0;
        }
    }
    return 1;
}

static int observe_resident_backing(volatile unsigned char *mapping, size_t bytes) {
    if (observe_backing((const void *)mapping, bytes)) {
        return 1;
    }
    /* One bounded PTE refresh, never a VM retry or an ignored failed snapshot. */
    return verify_after_pin(mapping, bytes) && observe_backing((const void *)mapping, bytes);
}

static int prepare_and_exec(int memory, int touch, int small_folios, int chunked, int pin_check,
                            int normal_first, int collapse) {
    if (!pinned_file(VMM, PLB_PREALLOCATED_VMM_SHA256) ||
        !pinned_file(GUEST_KERNEL, PLB_PREALLOCATED_KERNEL_SHA256) ||
        !pinned_file(INITRAMFS, PLB_PREALLOCATED_INITRAMFS_SHA256) ||
        !available(memory + (pin_check ? 768 : 0))) {
        return 81;
    }
    /* Make only this owned VMM an early OOM victim; never protect it over apps. */
    int oom = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
    if (oom < 0 || write(oom, "1000", 4) != 4) {
        return 82;
    }
    close(oom);
    int fd =
        (int)syscall(__NR_memfd_create, "plb-preallocated-guest", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        return 83;
    }
    int inherited = fcntl(fd, F_DUPFD, 32);
    close(fd);
    if (inherited != 32) {
        return 84;
    }
    size_t bytes = (size_t)memory * MIB;
    if (ftruncate(inherited, (off_t)bytes)) {
        return 85;
    }
    size_t batch = chunked ? 16UL * MIB : bytes;
    size_t collapse_attempts = 0, collapse_successes = 0;
    long long collapse_deadline = 0;
    for (size_t ordinal = 0; ordinal < bytes / batch; ordinal++) {
        size_t base = plb_preallocated_offset(ordinal, bytes, batch, normal_first);
        if (base == (size_t)-1) {
            return 86;
        }
        int aligned = collapse;
#ifdef PLB_PREALLOCATED_ANDROID_PROXY
        /* A file/VMA offset mismatch can exclude every requested mTHP size.
         * Reserve only fresh address space; never remap live backing or punch holes. */
        aligned = 1;
#endif
        volatile unsigned char *mapping =
            aligned ? plb_aligned_shared_mapping(inherited, batch, (off_t)base)
                    : mmap(NULL, batch, PROT_READ | PROT_WRITE, MAP_SHARED, inherited, (off_t)base);
        if (mapping == MAP_FAILED) {
            return 86;
        }
        if (small_folios && madvise((void *)mapping, batch, MADV_HUGEPAGE)) {
            return 86;
        }
        for (size_t at = 0; at < batch; at += 4096) {
            mapping[at] = 0;
            if (!(at % (64UL * MIB)) && (!plb_host_memory_healthy() || !available(0))) {
                return 87;
            }
        }
        /* The high guest zone is prepared first. Spend a bounded, process-local
         * collapse budget on low-file-offset backing; never refault a hole or
         * retry a failed request. Failed collapse preserves the original RAM. */
        if (collapse && base < 2048UL * MIB) {
            if (!collapse_deadline) {
                collapse_deadline = now_ms() + 3000;
            }
            for (size_t at = 0;
                 at < batch && collapse_attempts < 512 && now_ms() < collapse_deadline;
                 at += PLB_COLLAPSE_BYTES) {
                if (!plb_host_memory_healthy() || !available(0)) {
                    return 87;
                }
                collapse_attempts++;
                collapse_successes +=
                    !plb_collapse_owned((void *)(mapping + at), PLB_COLLAPSE_BYTES);
            }
        }
        printf("PLB_PREALLOCATED_BATCH offset_mib=%zu bytes=%zu\n", base / MIB, batch);
        if (!observe_resident_backing(mapping, batch) ||
            (pin_check && plb_preallocated_pin((void *)mapping, batch)) ||
            (pin_check && !verify_after_pin(mapping, batch)) ||
            (pin_check && !observe_resident_backing(mapping, batch)) ||
            munmap((void *)mapping, batch)) {
            return 88;
        }
    }
    if (collapse) {
        printf("PLB_PREALLOCATED_COLLAPSE_SUMMARY attempts=%zu successful_requests=%zu\n",
               collapse_attempts, collapse_successes);
    }
    if (fcntl(inherited, F_ADD_SEALS, 7)) {
        return 88;
    }
    struct stat info;
    if (fstat(inherited, &info)) {
        return 89;
    }
    char identity[96], size[32], options[256];
    snprintf(identity, sizeof(identity), "%" PRIu64 ":%" PRIu64 ":%zu", (uint64_t)info.st_dev,
             (uint64_t)info.st_ino, bytes);
    if (setenv("PLB_RAM_IDENTITY", identity, 1) ||
        setenv("LD_LIBRARY_PATH", "/apex/com.android.virt/lib64:/system/lib64", 1)) {
        return 90;
    }
    /* No LD_PRELOAD, custom module probe, file-backed/CMA option or policy knob. */
    unsetenv("LD_PRELOAD");
    snprintf(size, sizeof(size), "size=%d", memory);
    snprintf(options, sizeof(options),
             "console=ttyS0 8250.nr_uarts=1 rdinit=/init panic=0 quiet loglevel=3 "
             "plb.pressuremib=%d plb.rounds=1 plb.discard=0",
             touch);
    printf("PLB_PREALLOCATED_READY memory_mib=%d touch_mib=%d unmapped=1 sealed=1\n", memory,
           touch);
    if (plb_preallocated_notify_ready()) {
        return 92;
    }
#ifdef PLB_PREALLOCATED_ANDROID_PROXY
    const char *const *arguments = android4g_arguments;
#else
    const char *arguments[] = {VMM,
                               "--extended-status",
                               "run",
                               "--name",
                               "plb-preallocated-memory",
                               "--disable-sandbox",
                               "--protected-vm-without-firmware",
                               "--swiotlb",
                               "32",
                               "--mem",
                               size,
                               "--cpus",
                               "num-cores=1",
                               "--serial",
                               "type=stdout,console=true",
                               "--params",
                               options,
                               "--initrd",
                               INITRAMFS,
                               GUEST_KERNEL,
                               NULL};
#endif
    execv(VMM, (char *const *)arguments);
    return 91;
}

#ifndef PLB_PREALLOCATED_ANDROID_PROXY

static void observe_owned_child(pid_t child, int limits) {
    char path[96], line[256];
    snprintf(path, sizeof(path), "/proc/%ld/status", (long)child);
    FILE *input = fopen(path, "re");
    if (input) {
        fputs("PLB_PREALLOCATED_OWNED_STATUS\n", stderr);
        while (fgets(line, sizeof(line), input)) {
            if (!strncmp(line, "VmLck:", 6) || !strncmp(line, "VmPin:", 6) ||
                !strncmp(line, "VmRSS:", 6) || !strncmp(line, "CapEff:", 7)) {
                fputs(line, stderr);
            }
        }
        fclose(input);
    }
    if (limits) {
        snprintf(path, sizeof(path), "/proc/%ld/limits", (long)child);
        input = fopen(path, "re");
        if (input) {
            while (fgets(line, sizeof(line), input)) {
                if (!strncmp(line, "Max locked memory", 17)) {
                    fputs(line, stderr);
                }
            }
            fclose(input);
        }
    }
}

#endif

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef PLB_PREALLOCATED_ANDROID_PROXY
    (void)argv;
    if (argc != 1 || !platform() || !plb_host_memory_healthy()) {
        return 70;
    }
    /* Android boot allocates heavily from the low guest DMA32 zone.
     * Compare ascending preparation with the retained normal-first trial. */
    const int normal_first = 0, chunked = 1, small_folios = 1, pin_check = 0, collapse = 0;
    const long memory = PLB_ANDROID4G_MEMORY_MIB, touch = 0;
#else
    int normal_first_pin = argc == 4 && !strcmp(argv[3], "--normal-first-pin");
    int collapse = argc == 4 && !strcmp(argv[3], "--normal-first-collapse");
    int normal_first =
        normal_first_pin || collapse || (argc == 4 && !strcmp(argv[3], "--normal-first"));
    int chunked = normal_first || (argc == 4 && !strcmp(argv[3], "--chunked-folios"));
    int small_folios = chunked || (argc == 4 && !strcmp(argv[3], "--small-folios"));
    int pin_check = normal_first_pin || (argc == 4 && !strcmp(argv[3], "--pin-before-lend"));
    if ((argc != 3 && !small_folios && !pin_check) || !platform() || !plb_host_memory_healthy()) {
        return 70;
    }
    if (getenv(PLB_PREALLOCATED_READY_ENV) &&
        plb_ready_descriptor(getenv(PLB_PREALLOCATED_READY_ENV), 0) < 0) {
        return 70;
    }
    char *end;
    long memory = strtol(argv[1], &end, 10);
    if (!*argv[1] || *end || memory < 0 || memory > 5888) {
        return 71;
    }
    long touch = strtol(argv[2], &end, 10);
    if (!*argv[2] || *end || touch < 0 || touch > 4096 ||
        !plb_preallocated_workload_mode((int)memory, (int)touch, pin_check)) {
        return 72;
    }
#endif
    if (getenv(PLB_PREALLOCATED_READY_ENV) &&
        plb_ready_descriptor(getenv(PLB_PREALLOCATED_READY_ENV), 0) < 0) {
        return 70;
    }
#ifdef PLB_PREALLOCATED_ANDROID_PROXY
    int owner = diagnostic_owner_lock();
    if (owner < 0) {
        return 73;
    }
    // The manager supervises this same PID, retaining strict console peer checks.
    return prepare_and_exec((int)memory, (int)touch, small_folios, chunked, pin_check, normal_first,
                            collapse);
#else
    int owner = diagnostic_owner_lock();
    int monitor = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (owner < 0 || monitor < 0 || lseek(monitor, 0, SEEK_END) < 0) {
        return 73;
    }
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) {
            _exit(125);
        }
        close(monitor);
        _exit(prepare_and_exec((int)memory, (int)touch, small_folios, chunked, pin_check,
                               normal_first, collapse));
    }
    if (child < 0) {
        return 74;
    }
    const char *ready_text = getenv(PLB_PREALLOCATED_READY_ENV);
    if (ready_text) {
        int ready_fd = plb_ready_descriptor(ready_text, 0);
        unsetenv(PLB_PREALLOCATED_READY_ENV);
        if (ready_fd >= 0) {
            close(ready_fd); /* Only the worker may keep the notification writer. */
        }
    }
    int status = 0, result = 124;
    long long deadline = now_ms() + 40000;
    long long next_sample = now_ms() + 500;
    int sample_number = 0;
    while (now_ms() < deadline) {
        if (plb_diagnostic_kernel_bad(monitor) || !plb_host_memory_healthy()) {
            fprintf(stderr, "PLB_PREALLOCATED_HOST_FAILURE\n");
            result = 75;
            break;
        }
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            child = -1;
            break;
        }
        if (done < 0 && errno != EINTR) {
            result = 76;
            break;
        }
        if (stopping) {
            break;
        }
        if (sample_number < 4 && now_ms() >= next_sample) {
            /* The direct child is still unreaped, so its PID cannot be reused. */
            observe_owned_child(child, sample_number == 0);
            next_sample = now_ms() + 1000;
            sample_number++;
        }
        struct timespec delay = {.tv_nsec = 25000000};
        nanosleep(&delay, NULL);
    }
    if (child > 0) {
        kill(child, SIGTERM);
        for (int i = 0; i < 80; i++) {
            if (waitpid(child, &status, WNOHANG) == child) {
                child = -1;
                break;
            }
            usleep(25000);
        }
        if (child > 0) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
    close(monitor);
    close(owner);
    printf("PLB_PREALLOCATED_EXIT code=%d\n", result);
    return result;
#endif
}
