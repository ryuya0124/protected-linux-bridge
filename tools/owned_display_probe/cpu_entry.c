/* MIT. Fixed, diskless developer comparison against the source VMM.
 * The bounded CPU controller owns the lock, health monitor and child lifetime.
 * This entry does not initialize a renderer or change host memory policy. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "host_android17.h"
#include "cpu_source_vmm.generated.h"
#include "cpu_probe.generated.h"
#ifdef PLB_CPU_SHMEM_ADVISE
#include "diagnostic_owner_lock.h"
#include "preallocated_collapse.h"
#include "owned_page_summary.h"
#include "cpu_shmem.generated.h"
#ifdef PLB_CPU_PREPARE_RECLAIM
#include "probe_watchdog.h"
static int prepare_reclaim(int owner) {
    pid_t parent = getpid(), child = fork();
    if (!child) {
        char descriptor[32];
        snprintf(descriptor, sizeof(descriptor), "%d", owner);
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent || fcntl(owner, F_SETFD, 0) ||
            setenv(PLB_DIAGNOSTIC_OWNER_ENV, descriptor, 1))
            _exit(126);
        execl(PLB_CPU_RECLAIM_HELPER, "pressure-check", "detached", PLB_CPU_RECLAIM_MODE,
              (char *)NULL);
        _exit(127);
    }
    if (child < 0)
        return -1;
    return plb_probe_wait(child, 28000);
}
#endif

static int prepare_advised(int fd, size_t bytes, int owner) {
    struct plb_shmem_guard guard;
    if (plb_shmem_begin(&guard, owner))
        return -1;
    int result = 0;
    int pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    int flags = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    const size_t batch = 16 * 1024 * 1024;
    size_t thp = 0, pages = 0;
    for (size_t offset = 0; !result && offset < bytes; offset += batch) {
        volatile unsigned char *mapping = plb_aligned_shared_mapping(fd, batch, offset);
        if (mapping == MAP_FAILED) {
            result = -1;
            break;
        }
        if (madvise((void *)mapping, batch, MADV_HUGEPAGE))
            result = -1;
        for (size_t at = 0; !result && at < batch; at += 4096)
            mapping[at] = 0;
        struct plb_page_summary summary = {0};
        for (size_t at = 0; !result && at < batch; at += 4096) {
            uint64_t page = 0, bits = 0;
            if (pread(pagemap, &page, 8, ((uintptr_t)mapping + at) / 4096 * 8) != 8 ||
                !(page & (1ULL << 63)) || !(page & ((1ULL << 55) - 1)) ||
                pread(flags, &bits, 8, (page & ((1ULL << 55) - 1)) * 8) != 8) {
                result = -1;
                break;
            }
            plb_page_observe(&summary, 1, 1, page & ((1ULL << 55) - 1), 1, bits);
        }
        for (size_t at = 0; !result && at < batch; at++)
            if (mapping[at] != 0)
                result = -1;
        pages += summary.samples;
        thp += summary.thp_pages;
        printf("CPU_ADVISED_BACKING offset_mib=%zu pages=%zu thp=%zu heads=%zu contents=%d\n",
               offset / (1024 * 1024), summary.samples, summary.thp_pages, summary.compound_heads,
               !result);
        if (munmap((void *)mapping, batch))
            result = -1;
    }
    if (pagemap >= 0)
        close(pagemap);
    if (flags >= 0)
        close(flags);
    printf("CPU_ADVISED_TOTAL pages=%zu thp=%zu complete=%d\n", pages, thp,
           pages == bytes / 4096 && !result);
    if (plb_shmem_finish(&guard))
        result = -1;
    return result;
}
#endif

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getuid() || argc < 10 || argc > 80 || !host17_admitted())
        return 40;
    for (size_t i = 0; i < sizeof(cpu_vmm_pins) / sizeof(*cpu_vmm_pins); i++)
        if (!host17_digest(cpu_vmm_pins[i].path, cpu_vmm_pins[i].digest))
            return 41;
#ifdef PLB_CPU_SHMEM_ADVISE
    int owner = diagnostic_owner_lock();
    if (owner < 0)
        return 47;
#ifdef PLB_CPU_PREPARE_RECLAIM
    /* Release pressure before creating guest backing, so their configured
     * budgets never overlap. The helper checks only its own allocation. */
    if (prepare_reclaim(owner))
        return 48;
#endif
#endif
    /* Normal sparse ordinary-memfd backing, matching the stock constructor.
     * No prefault, advice, hole punching, CMA ioctl or allocation fallback. */
    const size_t bytes = PLB_CPU_RAM_BYTES;
    if (bytes != 512ULL * 1024 * 1024 && bytes != 1024ULL * 1024 * 1024 &&
        bytes != 1536ULL * 1024 * 1024)
        return 42;
    int fd = memfd_create("plb17-cpu-comparison", MFD_ALLOW_SEALING | MFD_CLOEXEC);
    struct stat metadata;
    if (fd < 0 || ftruncate(fd, bytes) || fcntl(fd, F_ADD_SEALS, 7) || fstat(fd, &metadata))
        return 42;
#ifdef PLB_CPU_SHMEM_ADVISE
    if (prepare_advised(fd, bytes, owner))
        return 47;
    close(owner);
#endif
    if (fd != 32) {
        if (dup3(fd, 32, 0) != 32)
            return 43;
        close(fd);
    } else if (fcntl(fd, F_SETFD, 0)) {
        return 43;
    }
    char identity[128];
    snprintf(identity, sizeof(identity), "%llu:%llu:%zu", (unsigned long long)metadata.st_dev,
             (unsigned long long)metadata.st_ino, bytes);
    if (setenv("PLB_RAM_IDENTITY", identity, 1))
        return 44;
    void *library = dlopen(PLB_CPU_SOURCE_VMM, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "CPU_SOURCE_LOAD_REFUSED %s\n", dlerror());
        return 45;
    }
    int (*entry)(int, const char *const *) = dlsym(library, "plb_crosvm_owned_display_main");
    if (!entry)
        return 46;
#ifdef PLB_CPU_SHMEM_ADVISE
    printf("CPU_SOURCE_ENTRY advised_prefault_memfd=1 bytes=%zu gpu_requested=0\n", bytes);
#else
    printf("CPU_SOURCE_ENTRY ordinary_sparse_memfd=1 bytes=%zu gpu_requested=0\n", bytes);
#endif
    /* This source build omits the balloon feature entirely, so its parser has
     * no --no-balloon option. Keep every other controller argument unchanged. */
    const char *arguments[81];
    int count = 0;
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "--no-balloon"))
            arguments[count++] = argv[i];
    arguments[count] = NULL;
    return entry(count, arguments);
}
