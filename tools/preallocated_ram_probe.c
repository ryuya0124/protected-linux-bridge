/* MIT. One 16 MiB owned memfd, unmap, exec a consumer, then inspect the same FD.
 * No VM, device ioctl, THP advice, policy change, pressure loop or hole punching.
 * The 15+1 MiB split only exercises offsets; it is not a Gunyah RAM layout.
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
#include <sys/wait.h>
#include <unistd.h>
#include "../native/profile_lock.h"
#include "host_memory_health.h"
#include "preallocated_ram_contract.h"
#include "probe_watchdog.h"

#define HANDOFF_FD 32
#define PAGE_BYTES 4096UL
#define PAGE_COUNT (PLB_HANDOFF_BYTES / PAGE_BYTES)
#define AVAILABLE_FLOOR_KIB (1536UL * 1024)

static uint64_t pattern(size_t word) {
    return UINT64_C(0x713bd4c9a208f65e) ^ ((uint64_t)word * UINT64_C(0x9e3779b97f4a7c15));
}

static int check_words(const uint64_t *mapping, size_t bytes, size_t offset) {
    for (size_t word = 0; word < bytes / sizeof(*mapping); word++) {
        if (mapping[word] != pattern(word + offset / sizeof(*mapping))) {
            return -1;
        }
    }
    return 0;
}

/* PFNs stay in this process's memory. Only aggregate comparisons are printed.
 * This observation does not pin memory or promise its layout at VM donation. */
static int snapshot_pages(void *mapping, uint64_t pages[PAGE_COUNT], const char *stage) {
    int pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    int flags_fd = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    size_t present = 0, thp = 0;
    int result = -1;
    if (pagemap < 0 || flags_fd < 0) {
        goto done;
    }
    for (size_t page = 0; page < PAGE_COUNT; page++) {
        uint64_t entry = 0, flags = 0;
        off_t index = (off_t)(((uintptr_t)mapping / PAGE_BYTES + page) * sizeof(entry));
        if (pread(pagemap, &entry, sizeof(entry), index) != sizeof(entry) ||
            !(entry & (UINT64_C(1) << 63))) {
            goto done;
        }
        uint64_t pfn = entry & ((UINT64_C(1) << 55) - 1);
        if (!pfn ||
            pread(flags_fd, &flags, sizeof(flags), (off_t)(pfn * sizeof(flags))) != sizeof(flags)) {
            goto done;
        }
        pages[page] = pfn;
        present++;
        thp += !!(flags & (UINT64_C(1) << 22));
    }
    result = 0;
done:
    if (pagemap >= 0) {
        close(pagemap);
    }
    if (flags_fd >= 0) {
        close(flags_fd);
    }
    printf("PLB_HANDOFF_PAGES stage=%s present=%zu thp=%zu complete=%d\n", stage, present, thp,
           !result);
    return result;
}

static int consume(uint64_t device, uint64_t inode) {
    struct stat actual;
    if (fstat(HANDOFF_FD, &actual) ||
        !plb_handoff_contract(&actual, fcntl(HANDOFF_FD, F_GETFL), fcntl(HANDOFF_FD, F_GET_SEALS),
                              device, inode)) {
        return 20;
    }
    /* Do not repeat F_ADD_SEALS after F_SEAL_SEAL; validate instead. */
    const size_t offsets[] = {0, PLB_HANDOFF_MAIN_BYTES};
    const size_t sizes[] = {PLB_HANDOFF_MAIN_BYTES, PLB_HANDOFF_BYTES - PLB_HANDOFF_MAIN_BYTES};
    for (size_t region = 0; region < 2; region++) {
        void *mapping = mmap(NULL, sizes[region], PROT_READ | PROT_WRITE, MAP_SHARED, HANDOFF_FD,
                             (off_t)offsets[region]);
        if (mapping == MAP_FAILED) {
            return 21;
        }
        int mismatch = check_words(mapping, sizes[region], offsets[region]);
        int unmap_result = munmap(mapping, sizes[region]);
        if (mismatch || unmap_result) {
            return 22;
        }
    }
    printf("PLB_HANDOFF_CONSUMER identity=1 size=1 seals=1 offsets=1 contents=1 bytes=%lu\n",
           PLB_HANDOFF_BYTES);
    return 0;
}

static int attach_parent_lifetime(pid_t expected) {
    return prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != expected;
}

static int produce(void) {
    int fd = (int)syscall(__NR_memfd_create, "plb-handoff-probe", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        return 10;
    }
    /* Never overwrite a pre-existing inherited descriptor. F_DUPFD deliberately
     * clears CLOEXEC only on the new descriptor used by our own consumer. */
    int inherited = fcntl(fd, F_DUPFD, HANDOFF_FD);
    if (inherited != HANDOFF_FD) {
        if (inherited >= 0) {
            close(inherited);
        }
        close(fd);
        return 11;
    }
    close(fd);
    if (ftruncate(HANDOFF_FD, PLB_HANDOFF_BYTES)) {
        return 12;
    }
    uint64_t *mapping =
        mmap(NULL, PLB_HANDOFF_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, HANDOFF_FD, 0);
    if (mapping == MAP_FAILED) {
        return 13;
    }
    for (size_t word = 0; word < PLB_HANDOFF_BYTES / sizeof(*mapping); word++) {
        mapping[word] = pattern(word);
    }
    uint64_t before[PAGE_COUNT], after[PAGE_COUNT];
    if (snapshot_pages(mapping, before, "prepared") || munmap(mapping, PLB_HANDOFF_BYTES) ||
        fcntl(HANDOFF_FD, F_ADD_SEALS, PLB_HANDOFF_REQUIRED_SEALS)) {
        return 14;
    }
    struct stat original;
    if (fstat(HANDOFF_FD, &original)) {
        return 15;
    }
    char device[32], inode[32];
    snprintf(device, sizeof(device), "%" PRIu64, (uint64_t)original.st_dev);
    snprintf(inode, sizeof(inode), "%" PRIu64, (uint64_t)original.st_ino);
    pid_t producer = getpid();
    pid_t child = fork();
    if (child < 0) {
        return 16;
    }
    if (!child) {
        if (attach_parent_lifetime(producer)) {
            _exit(125);
        }
        execl("/proc/self/exe", "preallocated-ram-probe", "--consume", device, inode, NULL);
        _exit(126);
    }
    int status = plb_probe_wait(child, 4000);
    if (status) {
        return status;
    }
    mapping = mmap(NULL, PLB_HANDOFF_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, HANDOFF_FD, 0);
    if (mapping == MAP_FAILED) {
        return 17;
    }
    if (check_words(mapping, PLB_HANDOFF_BYTES, 0) || snapshot_pages(mapping, after, "returned")) {
        munmap(mapping, PLB_HANDOFF_BYTES);
        return 18;
    }
    size_t same = 0;
    for (size_t page = 0; page < PAGE_COUNT; page++) {
        same += before[page] == after[page];
    }
    printf("PLB_HANDOFF_RETURN pages=%lu same_observed_pfn=%zu contents=1\n", PAGE_COUNT, same);
    int result = munmap(mapping, PLB_HANDOFF_BYTES);
    close(HANDOFF_FD);
    return result ? 19 : 0;
}

static int available_memory(void) {
    FILE *input = fopen("/proc/meminfo", "re");
    if (!input) {
        return 0;
    }
    char line[256];
    unsigned long available = 0;
    while (fgets(line, sizeof(line), input)) {
        if (sscanf(line, "MemAvailable: %lu kB", &available) == 1) {
            break;
        }
    }
    fclose(input);
    return available >= AVAILABLE_FLOOR_KIB + PLB_HANDOFF_BYTES / 1024;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char model[PROP_VALUE_MAX];
    if (geteuid() || sysconf(_SC_PAGESIZE) != PAGE_BYTES ||
        __system_property_get("ro.product.model", model) <= 0 || strcmp(model, "SM-S948Q") ||
        !plb_host_memory_healthy()) {
        fprintf(stderr, "PLB_HANDOFF_REFUSED platform_or_host_health\n");
        return 80;
    }
    if (argc == 4 && !strcmp(argv[1], "--consume")) {
        uint64_t values[2];
        for (size_t i = 0; i < 2; i++) {
            char *end;
            errno = 0;
            values[i] = strtoull(argv[i + 2], &end, 10);
            if (errno || !*argv[i + 2] || *end) {
                return 81;
            }
        }
        return consume(values[0], values[1]);
    }
    if (argc != 1) {
        return 82;
    }
    int owner = profile_lock();
    if (owner < 0 || !available_memory()) {
        if (owner >= 0) {
            close(owner);
        }
        return 83;
    }
    pid_t controller = getpid();
    pid_t worker = fork();
    if (!worker) {
        if (attach_parent_lifetime(controller)) {
            _exit(125);
        }
        _exit(produce());
    }
    int result = worker < 0 ? 84 : plb_probe_wait(worker, 10000);
    close(owner);
    printf("PLB_HANDOFF_EXIT code=%d\n", result);
    return result;
}
