/* MIT. Small, process-local backing allocation experiment; no VM or device ioctl.
 * Default: 16 MiB, one short-range check and two owned 2 MiB collapse requests.
 * Explicit detached/reclaim-4g mode additionally touches/frees at most 4 GiB.
 * A direct parent watchdog supervises only its own worker. No sysfs,
 * global compaction, THP policy, kernel, partition or protection setting writes.
 */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <linux/mount.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../native/profile_lock.h"
#include "owned_page_summary.h"
#include "probe_watchdog.h"
#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif

static void accounting(const char *stage) {
    FILE *input = fopen("/proc/self/smaps_rollup", "re");
    char line[256];
    printf("PLB_ALLOCATION_STAGE %s\n", stage);
    if (!input)
        return;
    while (fgets(line, sizeof(line), input))
        if (!strncmp(line, "Rss:", 4) || !strncmp(line, "AnonHugePages:", 14) ||
            !strncmp(line, "ShmemPmdMapped:", 15) || !strncmp(line, "Anonymous:", 10))
            fputs(line, stdout);
    fclose(input);
    input = fopen("/proc/self/status", "re");
    if (input) {
        while (fgets(line, sizeof(line), input)) {
            if (!strncmp(line, "VmSize:", 7) || !strncmp(line, "VmSwap:", 7)) {
                fputs(line, stdout);
            }
        }
        fclose(input);
    }
    fflush(stdout);
}

static void owned_page_flags(void *mapping, size_t bytes) {
    int pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    int flags_fd = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    struct plb_page_summary summary = {0};
    if (pagemap < 0 || flags_fd < 0) {
        printf("PLB_OWNED_PAGE_FLAGS unavailable=1\n");
        goto done;
    }
    for (size_t at = 0; at < bytes; at += 4096) {
        uint64_t entry = 0, flags = 0;
        off_t index = (off_t)(((uintptr_t)mapping + at) / 4096 * 8);
        int read_ok = pread(pagemap, &entry, 8, index) == 8;
        int present = read_ok && !!(entry & (1ULL << 63));
        uint64_t pfn = entry & ((1ULL << 55) - 1);
        int flags_ok = present && pfn && pread(flags_fd, &flags, 8, (off_t)(pfn * 8)) == 8;
        plb_page_observe(&summary, present, pfn != 0, pfn, flags_ok, flags);
    }
    /* Aggregate only. The sampled PFNs may migrate between reads; this is not
     * a long-term pin or a promise of folio coverage at later VM donation. */
    printf(
        "PLB_OWNED_PAGE_FLAGS sampled=%zu present=%zu hidden=%zu flag_errors=%zu "
        "thp=%zu compound_head=%zu compound_tail=%zu max_forward_pages=%zu max_reverse_pages=%zu "
        "sampled_2m_folios=%zu\n",
        summary.samples, summary.present, summary.unknown_pfn, summary.flag_errors,
        summary.thp_pages, summary.compound_heads, summary.compound_tails, summary.longest_run,
        summary.longest_reverse_run, summary.sampled_2m_folios);
done:
    if (pagemap >= 0) {
        close(pagemap);
    }
    if (flags_fd >= 0) {
        close(flags_fd);
    }
}

static void mapping_accounting(void *mapping) {
    FILE *input = fopen("/proc/self/smaps", "re");
    char line[512];
    int selected = 0;
    if (!input) {
        return;
    }
    while (fgets(line, sizeof(line), input)) {
        unsigned long begin, end;
        if (sscanf(line, "%lx-%lx", &begin, &end) == 2) {
            selected = (uintptr_t)mapping >= begin && (uintptr_t)mapping < end;
        } else if (selected &&
                   (!strncmp(line, "THPeligible:", 12) || !strncmp(line, "KernelPageSize:", 15) ||
                    !strncmp(line, "MMUPageSize:", 12) || !strncmp(line, "VmFlags:", 8))) {
            fputs(line, stdout);
        }
    }
    fclose(input);
}

static const char *counter_names[] = {
    "thp_fault_alloc",   "thp_fault_fallback", "thp_file_alloc",
    "thp_file_fallback", "thp_collapse_alloc", "thp_collapse_alloc_failed",
    "compact_stall",     "compact_fail",       "compact_success"};
#define COUNTER_COUNT (sizeof(counter_names) / sizeof(counter_names[0]))

static void counters(long long values[COUNTER_COUNT]) {
    for (size_t i = 0; i < COUNTER_COUNT; i++) {
        values[i] = -1;
    }
    FILE *input = fopen("/proc/vmstat", "re");
    if (!input) {
        return;
    }
    char name[96];
    long long value;
    while (fscanf(input, "%95s %lld", name, &value) == 2) {
        for (size_t i = 0; i < COUNTER_COUNT; i++) {
            if (!strcmp(name, counter_names[i])) {
                values[i] = value;
            }
        }
    }
    fclose(input);
}

static void collapse_request(void *mapping, size_t length, const char *label) {
    long long before[COUNTER_COUNT], after[COUNTER_COUNT];
    struct timespec start, end;
    counters(before);
    clock_gettime(CLOCK_MONOTONIC, &start);
    errno = 0;
    int result = madvise(mapping, length, MADV_COLLAPSE);
    int saved = errno;
    clock_gettime(CLOCK_MONOTONIC, &end);
    counters(after);
    long long elapsed =
        (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_nsec - start.tv_nsec) / 1000;
    printf("PLB_ALLOCATION_COLLAPSE label=%s bytes=%zu result=%d errno=%d elapsed_us=%lld\n", label,
           length, result, saved, elapsed);
    for (size_t i = 0; i < COUNTER_COUNT; i++) {
        if (before[i] >= 0 && after[i] >= before[i]) {
            printf("PLB_GLOBAL_COUNTER_DELTA %s=%lld\n", counter_names[i], after[i] - before[i]);
        }
    }
    fflush(stdout);
}

static void *aligned_mapping(size_t size, size_t alignment, int shared, int fd) {
    /* Reserve first, then replace only an interior owned range. File offset 0
     * and virtual address have matching PMD alignment for the shmem case. */
    void *reservation = mmap(NULL, size + alignment, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reservation == MAP_FAILED) {
        return MAP_FAILED;
    }
    uintptr_t begin = (uintptr_t)reservation;
    uintptr_t aligned = (begin + alignment - 1) & ~(alignment - 1);
    int flags = MAP_FIXED | (shared ? MAP_SHARED : MAP_PRIVATE | MAP_ANONYMOUS);
    void *mapped = mmap((void *)aligned, size, PROT_READ | PROT_WRITE, flags, fd, 0);
    if (mapped == MAP_FAILED) {
        int saved = errno;
        munmap(reservation, size + alignment);
        errno = saved;
        return MAP_FAILED;
    }
    if (aligned > begin) {
        munmap(reservation, aligned - begin);
    }
    size_t tail = begin + size + alignment - (aligned + size);
    if (tail) {
        munmap((void *)(aligned + size), tail);
    }
    return mapped;
}

static int detached_backing(void) {
    int config = (int)syscall(SYS_fsopen, "tmpfs", FSOPEN_CLOEXEC);
    if (config < 0)
        return -1;
    if (syscall(SYS_fsconfig, config, FSCONFIG_SET_STRING, "huge", "always", 0) ||
        syscall(SYS_fsconfig, config, FSCONFIG_SET_STRING, "size", "16m", 0) ||
        syscall(SYS_fsconfig, config, FSCONFIG_CMD_CREATE, NULL, NULL, 0)) {
        close(config);
        return -1;
    }
    /* Detached mount FD only. No attach, mountpoint, mount namespace change,
     * mount propagation, or alteration of the host's internal shmem mount. */
    int mountfd = (int)syscall(SYS_fsmount, config, FSMOUNT_CLOEXEC,
                               MOUNT_ATTR_NODEV | MOUNT_ATTR_NOEXEC | MOUNT_ATTR_NOSUID);
    close(config);
    if (mountfd < 0)
        return -1;
    int fd = openat(mountfd, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);
    close(mountfd);
    return fd;
}

static unsigned long available_kib(void) {
    FILE *input = fopen("/proc/meminfo", "re");
    char line[256];
    unsigned long available = 0;
    if (!input) {
        return 0;
    }
    while (fgets(line, sizeof(line), input)) {
        if (sscanf(line, "MemAvailable: %lu kB", &available) == 1) {
            break;
        }
    }
    fclose(input);
    return available;
}

static uint64_t pressure_pattern(uint64_t value) {
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    return value;
}

static int bounded_reclaim_trial(void) {
    /* Explicit opt-in only. The user permits ordinary reclamation of other
     * apps. Give this process the highest OOM victim score; never change another
     * process or any global OOM/reclaim setting. No mlock or global compaction.
     */
    int score = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
    if (score < 0) {
        return -1;
    }
    int ok = write(score, "1000", 4) == 4;
    close(score);
    if (!ok) {
        return -1;
    }
    const size_t cap = 4096UL * 1024 * 1024, batch = 64UL * 1024 * 1024;
    unsigned char *pressure =
        mmap(NULL, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pressure == MAP_FAILED) {
        printf("PLB_PRESSURE_MAPPING_FAILED errno=%d\n", errno);
        return -1;
    }
    size_t touched = 0;
    for (; touched < cap; touched += batch) {
        if (available_kib() < 512UL * 1024) {
            break;
        }
        for (size_t page = touched; page < touched + batch; page += 4096) {
            uint64_t state = 0x9e3779b97f4a7c15ULL ^ (page / 4096);
            uint64_t *words = (uint64_t *)(pressure + page);
            for (size_t word = 0; word < 4096 / sizeof(*words); word++) {
                state = pressure_pattern(state);
                words[word] = state;
            }
        }
        if ((touched + batch) % (256UL * 1024 * 1024) == 0) {
            printf("PLB_PRESSURE_PROGRESS mib=%zu available_kib=%lu\n",
                   (touched + batch) / (1024 * 1024), available_kib());
            fflush(stdout);
        }
    }
    size_t errors = 0;
    for (size_t at = 0; at < touched; at += 4096) {
        uint64_t expected = pressure_pattern(0x9e3779b97f4a7c15ULL ^ (at / 4096));
        if (*(volatile uint64_t *)(pressure + at) != expected) {
            errors++;
        }
    }
    accounting("pressure-held");
    munmap(pressure, cap);
    printf("PLB_PRESSURE_RELEASED mib=%zu sampled_readback_errors=%zu available_kib=%lu\n",
           touched / (1024 * 1024), errors, available_kib());
    accounting("pressure-released");
    fflush(stdout);
    return errors ? -1 : 0;
}

int main(int argc, char **argv) {
    if (getuid() || sysconf(_SC_PAGESIZE) != 4096 || (argc != 2 && argc != 3) ||
        (strcmp(argv[1], "anon") && strcmp(argv[1], "shared") && strcmp(argv[1], "detached")))
        return 20;
    int reclaim = argc == 3;
    if (reclaim && (strcmp(argv[1], "detached") || strcmp(argv[2], "reclaim-4g"))) {
        return 20;
    }
    int owner_lock = profile_lock();
    if (owner_lock < 0) {
        fprintf(stderr, "PLB_ALLOCATION_REJECTED owner_lock errno=%d\n", errno);
        return 24;
    }
    fflush(NULL);
    pid_t parent = getpid();
    pid_t worker = fork();
    if (worker < 0) {
        close(owner_lock);
        return 26;
    }
    if (worker > 0) {
        int result = plb_probe_wait(worker, reclaim ? 30000 : 15000);
        close(owner_lock);
        return result;
    }
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) {
        _exit(27);
    }
    sigset_t signals;
    sigemptyset(&signals);
    sigprocmask(SIG_SETMASK, &signals, NULL);
    signal(SIGALRM, SIG_DFL);
    alarm(reclaim ? 25 : 10);
    printf("PLB_ALLOCATION_CONTEXT thp_disabled=%d\n", prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0));
    const size_t size = 16 * 1024 * 1024, huge = 2 * 1024 * 1024;
    int shared = strcmp(argv[1], "anon") != 0;
    int fd = -1;
    if (shared) {
        fd = !strcmp(argv[1], "detached")
                 ? detached_backing()
                 : (int)syscall(SYS_memfd_create, "plb-allocation-probe", 3);
        if (fd < 0 || ftruncate(fd, size)) {
            printf("PLB_ALLOCATION_BACKING_FAILED errno=%d\n", errno);
            return 21;
        }
    }
    if (fd >= 0) {
        int before_seals = fcntl(fd, F_GET_SEALS);
        errno = 0;
        int add_seals = fcntl(fd, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL);
        int saved = errno;
        printf("PLB_ALLOCATION_SEALS before=%d add_result=%d errno=%d after=%d\n", before_seals,
               add_seals, saved, fcntl(fd, F_GET_SEALS));
    }
    void *map = aligned_mapping(size, huge, shared, fd);
    if (map == MAP_FAILED)
        return 22;
    printf("PLB_ALLOCATION_ALIGNMENT virtual_2m=%d file_offset_2m=1\n", (uintptr_t)map % huge == 0);
    int advice = madvise(map, size, MADV_HUGEPAGE);
    printf("PLB_ALLOCATION_ADVICE mode=%s result=%d\n", argv[1], advice);
    volatile unsigned char *bytes = map;
    for (size_t at = 0; at < size; at += 4096)
        bytes[at] = 0x5a;
    accounting("populated");
    owned_page_flags(map, size);
    mapping_accounting(map);
    if (reclaim && bounded_reclaim_trial()) {
        munmap(map, size);
        if (fd >= 0) {
            close(fd);
        }
        close(owner_lock);
        return 25;
    }
    if (reclaim) {
        /* Pressure may have swapped out this target. Restore residency before
         * attributing a later failure to a resident collapse candidate. */
        for (size_t at = 0; at < size; at += 4096) {
            if (bytes[at] != 0x5a) {
                return 28;
            }
            bytes[at] = 0x5a;
        }
        accounting("target-repopulated");
        owned_page_flags(map, size);
    }
    /* First isolate the short-range return-code ambiguity. A zero return here
     * is never treated as proof of a 64 KiB folio; page flags decide. */
    collapse_request(map, 64 * 1024, "short-64k");
    owned_page_flags(map, size);
    for (unsigned n = 0; n < 2; n++) {
        collapse_request((unsigned char *)map + n * huge, huge, "full-2m");
    }
    accounting("collapse-requested");
    owned_page_flags(map, size);
    unsigned errors = 0;
    for (size_t at = 0; at < size; at += 4096)
        if (bytes[at] != 0x5a)
            errors++;
    printf("PLB_ALLOCATION_READBACK errors=%u\n", errors);
    munmap(map, size);
    if (fd >= 0)
        close(fd);
    close(owner_lock);
    return errors ? 23 : 0;
}
