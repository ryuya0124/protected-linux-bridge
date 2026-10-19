/* MIT. Bounded diskless guest RAM probe. Never run as an Android program. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/sysinfo.h>
#include <sys/auxv.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "guest_page_tracker.h"

static void finish(void) {
    fflush(stdout);
    reboot(RB_POWER_OFF);
    for (;;)
        pause();
}

/* Read only this guest process's page table. Batches keep observation overhead
 * bounded; the bitmap holds no host PFNs and is never exported. */
static int observe_pages(int pagemap, const void *memory, size_t bytes, size_t page_size,
                         unsigned char *seen, size_t pfn_limit, size_t *unique, int round) {
    uint64_t entries[256];
    size_t pages = bytes / page_size;
    struct plb_guest_page_scan scan = {0};
    for (size_t at = 0; at < pages; at += 256) {
        size_t count = pages - at < 256 ? pages - at : 256;
        off_t offset = (off_t)(((uintptr_t)memory / page_size + at) * 8);
        if (pread(pagemap, entries, count * 8, offset) != (ssize_t)(count * 8))
            return -1;
        for (size_t i = 0; i < count; i++)
            plb_guest_page_observe(&scan, entries[i], seen, pfn_limit);
    }
    *unique += scan.new_pages;
    printf("PLB_MEMORY_PAGES round=%d present=%zu absent=%zu hidden=%zu outside=%zu "
           "new_pages=%zu cumulative_unique_pages=%zu guest_page=%zu\n",
           round, scan.present, scan.absent, scan.hidden, scan.outside, scan.new_pages, *unique,
           page_size);
    return scan.samples == pages && scan.present == pages && !scan.absent && !scan.hidden &&
                   !scan.outside
               ? 0
               : -1;
}

/* Sample each freshly written 64 MiB chunk before the next potentially failing
 * fault. Relative guest bands connect placement to pre-donation host bands. */
static void observe_progress_bands(int pagemap, const void *memory, size_t bytes, size_t page_size,
                                   size_t progress_mib) {
    size_t counts[PLB_GUEST_RAM_BANDS] = {0}, unknown = 0;
    uint64_t entries[256];
    size_t pages = bytes / page_size;
    for (size_t at = 0; at < pages; at += 256) {
        size_t count = pages - at < 256 ? pages - at : 256;
        off_t offset = (off_t)(((uintptr_t)memory / page_size + at) * 8);
        if (pread(pagemap, entries, count * 8, offset) != (ssize_t)(count * 8)) {
            unknown += count;
            continue;
        }
        for (size_t i = 0; i < count; i++) {
            int band = plb_guest_page_band(entries[i], page_size);
            if (band < 0)
                unknown++;
            else
                counts[band]++;
        }
    }
    printf("PLB_MEMORY_PLACEMENT after_mib=%zu unknown=%zu", progress_mib, unknown);
    for (size_t band = 0; band < PLB_GUEST_RAM_BANDS; band++)
        if (counts[band])
            printf(" offset_%zu_mib_pages=%zu", band * 256, counts[band]);
    putchar('\n');
}

int main(void) {
    if (getpid() != 1)
        return 99;
    int console = open("/dev/console", O_RDWR);
    if (console < 0)
        return 98;
    for (int i = 0; i < 3; i++)
        dup2(console, i);
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Linux advertises safe EL0 ID-register emulation with HWCAP_CPUID. */
    if (getauxval(AT_HWCAP) & (1UL << 11)) {
        uint64_t features;
        __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(features));
        printf("PLB_MEMORY_CPU id_aa64mmfr0_el1=%016llx\n", (unsigned long long)features);
    }
    if (mount("proc", "/proc", "proc", 0, NULL))
        finish();
    if (!mount("sysfs", "/sys", "sysfs", 0, NULL)) {
        FILE *features =
            fopen("/sys/devices/system/cpu/cpu0/regs/identification/id_aa64mmfr0", "r");
        char value[64];
        if (features && fgets(value, sizeof(value), features))
            printf("PLB_MEMORY_CPU id_aa64mmfr0=%s", value);
        if (features)
            fclose(features);
    }
    char cmdline[4096] = {0};
    FILE *options = fopen("/proc/cmdline", "r");
    if (!options || !fgets(cmdline, sizeof(cmdline), options))
        finish();
    fclose(options);
    const char *option = strstr(cmdline, " plb.pressuremib=");
    int mib = option ? atoi(option + strlen(" plb.pressuremib=")) : 0;
    option = strstr(cmdline, " plb.rounds=");
    int rounds = option ? atoi(option + strlen(" plb.rounds=")) : 1;
    option = strstr(cmdline, " plb.discard=");
    int discard = option ? atoi(option + strlen(" plb.discard=")) : 0;
    long page_size = sysconf(_SC_PAGESIZE);
    struct sysinfo info;
    if (mib < 64 || mib > 4096 || rounds < 1 || rounds > 8 || mib * rounds > 4096 ||
        (mib > 1024 && rounds != 1) || discard < 0 || discard > 1 || (discard && rounds < 2) ||
        (page_size != 4096 && page_size != 16384) || sysinfo(&info) ||
        (uint64_t)info.freeram * info.mem_unit < ((uint64_t)mib + 48) * 1048576) {
        puts("PLB_MEMORY_TARGET_REJECTED");
        finish();
    }
    printf("PLB_MEMORY_LAYOUT total_kib=%llu free_kib=%llu\n",
           (unsigned long long)((uint64_t)info.totalram * info.mem_unit / 1024),
           (unsigned long long)((uint64_t)info.freeram * info.mem_unit / 1024));
    size_t bytes = (size_t)mib * 1048576;
    volatile unsigned char *memory =
        mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        puts("PLB_MEMORY_MMAP_FAILED");
        finish();
    }
    size_t pfn_limit = (32ULL << 30) / (size_t)page_size;
    unsigned char *seen = calloc((pfn_limit + 7) / 8, 1);
    unsigned char *residency = calloc(bytes / (size_t)page_size, 1);
    int pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    if (!seen || !residency || pagemap < 0) {
        puts("PLB_MEMORY_OBSERVATION_FAILED");
        finish();
    }
    size_t unique_pages = 0;
    printf("PLB_MEMORY_BEGIN mib=%d guest_page=%ld\n", mib, page_size);
    printf("PLB_MEMORY_WORKLOAD rounds=%d allocation_mib=%d discard=%d\n", rounds, mib, discard);
    unsigned errors = 0;
    /* Resident mode retains the same pages. Discard mode frees only the guest's
     * anonymous pages, verifies their absence and checks zero-fill next round.
     * Neither operation asks the host/Gunyah to reclaim protected pages. */
    for (int round = 1; round <= rounds; round++) {
        for (size_t offset = 0; offset < bytes; offset += 4096) {
            if (discard && round > 1 && memory[offset] != 0)
                errors++;
            memory[offset] = (unsigned char)((offset / 4096 + (size_t)round) % 251 + 1);
            if ((offset + 4096) % (64 * 1048576) == 0) {
                printf("PLB_MEMORY_PROGRESS round=%d mib=%zu\n", round, (offset + 4096) / 1048576);
                observe_progress_bands(pagemap,
                                       (const void *)(memory + offset + 4096 - 64 * 1048576),
                                       64 * 1048576, (size_t)page_size, (offset + 4096) / 1048576);
                struct timespec step = {0, mib > 1024 ? 50000000 : 200000000};
                nanosleep(&step, NULL);
            }
        }
        for (size_t offset = 0; offset < bytes; offset += 4096)
            if (memory[offset] != (unsigned char)((offset / 4096 + (size_t)round) % 251 + 1))
                errors++;
        if (observe_pages(pagemap, (const void *)memory, bytes, (size_t)page_size, seen, pfn_limit,
                          &unique_pages, round)) {
            puts("PLB_MEMORY_OBSERVATION_FAILED");
            finish();
        }
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage)) {
            puts("PLB_MEMORY_USAGE_FAILED");
            finish();
        }
        printf("PLB_MEMORY_ROUND round=%d errors=%u minor_faults=%ld major_faults=%ld "
               "maxrss_kib=%ld\n",
               round, errors, usage.ru_minflt, usage.ru_majflt, usage.ru_maxrss);
        if (errors) {
            puts("PLB_MEMORY_FAILED");
            finish();
        }
        if (discard && round < rounds) {
            if (madvise((void *)memory, bytes, MADV_DONTNEED) ||
                mincore((void *)memory, bytes, residency)) {
                puts("PLB_MEMORY_DISCARD_FAILED");
                finish();
            }
            size_t present = 0;
            for (size_t i = 0; i < bytes / (size_t)page_size; i++)
                present += !!(residency[i] & 1);
            printf("PLB_MEMORY_DISCARD round=%d resident_pages=%zu\n", round, present);
            if (present) {
                puts("PLB_MEMORY_DISCARD_FAILED");
                finish();
            }
        }
    }
    close(pagemap);
    free(seen);
    free(residency);
    printf("PLB_MEMORY_TOUCHED mib=%d\n", mib);
    printf("PLB_MEMORY_READBACK errors=%u\n", errors);
    struct timespec hold = {5, 0};
    nanosleep(&hold, NULL); /* Allow the host to sample only this VMM's private mappings. */
    munmap((void *)memory, bytes);
    puts(errors ? "PLB_MEMORY_FAILED" : "PLB_MEMORY_COMPLETE");
    finish();
}
