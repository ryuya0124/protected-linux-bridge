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

static void finish(void) {
    fflush(stdout);
    reboot(RB_POWER_OFF);
    for (;;)
        pause();
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
    struct sysinfo info;
    if (mib < 64 || mib > 1024 || rounds < 1 || rounds > 8 || mib * rounds > 3072 ||
        sysinfo(&info) || (uint64_t)info.freeram * info.mem_unit < ((uint64_t)mib + 48) * 1048576) {
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
    printf("PLB_MEMORY_BEGIN mib=%d guest_page=%ld\n", mib, sysconf(_SC_PAGESIZE));
    printf("PLB_MEMORY_WORKLOAD rounds=%d unique_mib=%d\n", rounds, mib);
    unsigned errors = 0;
    /* Every round reuses the same resident allocation, without discard/unmap.
     * A guest without disks/swap cannot turn repeated coverage into a larger
     * working set. Fault counters make accidental new allocation observable. */
    for (int round = 1; round <= rounds; round++) {
        for (size_t offset = 0; offset < bytes; offset += 4096) {
            memory[offset] = (unsigned char)((offset / 4096 + (size_t)round) % 251 + 1);
            if ((offset + 4096) % (64 * 1048576) == 0) {
                printf("PLB_MEMORY_PROGRESS round=%d mib=%zu\n", round, (offset + 4096) / 1048576);
                struct timespec step = {0, 200000000};
                nanosleep(&step, NULL);
            }
        }
        for (size_t offset = 0; offset < bytes; offset += 4096)
            if (memory[offset] != (unsigned char)((offset / 4096 + (size_t)round) % 251 + 1))
                errors++;
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
    }
    printf("PLB_MEMORY_TOUCHED mib=%d\n", mib);
    printf("PLB_MEMORY_READBACK errors=%u\n", errors);
    struct timespec hold = {5, 0};
    nanosleep(&hold, NULL); /* Allow the host to sample only this VMM's private mappings. */
    munmap((void *)memory, bytes);
    puts(errors ? "PLB_MEMORY_FAILED" : "PLB_MEMORY_COMPLETE");
    finish();
}
