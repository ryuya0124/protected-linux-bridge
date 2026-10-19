/* MIT. Process-scoped guest-RAM backing; diagnostic variants remain separate.
 * The explicitly selected Android app variant keeps the original shared memfd
 * contract and requires complete middle-range THP observations before donation.
 * This library never changes a host policy, ioctl or protected VM type.
 * Private anonymous backing is diskless research only and rejects GPU, disks,
 * vsock, network and input. The separate Android variants accept only their
 * pinned RAM/renderer profiles and never replace shared RAM with it.
 */
#define _GNU_SOURCE
#ifdef PLB_ANDROID_LARGE_BACKING
#define PLB_ANDROID_SHARED_BACKING_PROBE 1
#include "android_large_config.h"
#endif
#ifdef PLB_ANDROID_GPU_BACKING
#define PLB_ANDROID_SHARED_BACKING_PROBE 1
#include "android_gpu_memory_config.h"
#endif
#if defined(PLB_ANDROID_LARGE_BACKING) || defined(PLB_ANDROID_GPU_BACKING)
#define PLB_PREFAULT_ONLY_BACKING 1
#endif
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include "backing_order_summary.h"
#include "backing_page_order.h"
#include "owned_page_summary.h"
#include "host_memory_health.h"
#ifndef PLB_PREFAULT_ONLY_BACKING
#include "shmem_refault_probe.h"
#endif
#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif

static int enabled;
static int prefault;
static int shared_backing;
static int reverse_order;
static int boot_safe;
static int refault_small;
static int collapse_backing;
static int tail_safe;
static int lock_backing;
static void *boot_safe_base;
static size_t boot_safe_size;
#define BOOT_BASE_PAGES (64UL * 1024 * 1024)
#define BOOT_TAIL_PAGES (tail_safe ? BOOT_BASE_PAGES : 0)

#ifndef PLB_PREFAULT_ONLY_BACKING
struct observed_boot_config {
    uint64_t guest_phys_addr, size;
};
struct observed_memory_region {
    uint32_t label, flags;
    uint64_t guest_phys_addr, memory_size, userspace_addr;
};

/* These layouts were checked against the installed host's offline BTF.
 * Command numbers come from the pinned public crosvm Gunyah bindings.
 * Observe only requests the original VMM already makes; never edit/reissue one. */
int ioctl(int fd, int request, ...) {
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    int result = (int)syscall(SYS_ioctl, fd, request, argument);
    int saved_errno = errno;
    if (boot_safe && !result && argument) {
        if ((unsigned)request == _IOW(71, 2, struct observed_boot_config)) {
            struct observed_boot_config config;
            memcpy(&config, argument, sizeof(config));
            dprintf(STDERR_FILENO, "PLB_BOOT_CONFIG guest=0x%llx size=0x%llx\n",
                    (unsigned long long)config.guest_phys_addr, (unsigned long long)config.size);
        } else if ((unsigned)request == _IOW(65, 0x11, struct observed_memory_region) ||
                   (unsigned)request == _IOW(71, 1, struct observed_memory_region)) {
            struct observed_memory_region region;
            memcpy(&region, argument, sizeof(region));
            dprintf(STDERR_FILENO,
                    "PLB_MEMORY_BINDING guest=0x%llx size=0x%llx host=0x%llx flags=%u lend=%d\n",
                    (unsigned long long)region.guest_phys_addr,
                    (unsigned long long)region.memory_size,
                    (unsigned long long)region.userspace_addr, region.flags,
                    (unsigned)request == _IOW(65, 0x11, struct observed_memory_region));
        }
    }
    errno = saved_errno;
    return result;
}
#endif

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
#ifdef PLB_ANDROID_SHARED_BACKING_PROBE
    int gpu = 0, disks = 0, vsock = 0, memory = 0, initrd = 0;
    const char *profile =
#ifdef PLB_ANDROID_LARGE_BACKING
        "/data/user/0/net.ryuya_dev.protectedlinux/files/android-guests/" PLB_LARGE_DIRECTORY;
#elif defined(PLB_ANDROID_GPU_BACKING)
        "/data/user/0/net.ryuya_dev.protectedlinux/files/android-guests/" PLB_GPU_MEMORY_DIRECTORY;
#else
        "/data/user/0/net.ryuya_dev.protectedlinux/files/android-guests/cf17-mem";
#endif
#endif
    const char *allowed[] = {
        "--name",       "--disable-sandbox",
        "--no-balloon", "--protected-vm-without-firmware",
        "--swiotlb",    "--mem",
        "--cpus",       "--serial",
        "--params",     "--initrd",
        "--hugepages",
#ifdef PLB_ANDROID_SHARED_BACKING_PROBE
        "--block",      "--gpu",
        "--vsock",      "--no-usb",
#endif
    };
    for (ssize_t at = 0; at < size;) {
        const char *arg = args + at;
        size_t length = strlen(arg);
        if (!strcmp(arg, "--name") && at + length + 1 < (size_t)size &&
            !strcmp(arg + length + 1,
#ifdef PLB_ANDROID_LARGE_BACKING
                    "plb-android17-large"
#elif defined(PLB_ANDROID_SHARED_BACKING_PROBE)
                    "plb-android17-probe"
#else
                    "plb-memory-probe"
#endif
                    ))
            named = 1;
        if (!strcmp(arg, "--protected-vm-without-firmware"))
            protected = 1;
        if (!strcmp(arg, "--no-balloon"))
            no_balloon = 1;
        if (!strcmp(arg, "--disable-sandbox"))
            no_sandbox = 1;
#ifdef PLB_ANDROID_SHARED_BACKING_PROBE
        const char *value = at + length + 1 < (size_t)size ? arg + length + 1 : "";
        if (!strcmp(arg, "--gpu")) {
            if (strcmp(value,
#ifdef PLB_ANDROID_GPU_BACKING
                       PLB_GPU_MEMORY_GPU_ARGUMENT
#else
                       "backend=2d,width=480,height=800"
#endif
                       ))
                return 0;
            gpu++;
        }
        if (!strcmp(arg, "--vsock")) {
            if (strcmp(value, "cid=39821"))
                return 0;
            vsock++;
        }
        if (!strcmp(arg, "--mem")) {
            if (
#ifdef PLB_ANDROID_LARGE_BACKING
                strcmp(value, PLB_LARGE_MEMORY_ARGUMENT)
#elif defined(PLB_ANDROID_GPU_BACKING)
                strcmp(value, PLB_GPU_MEMORY_ARGUMENT)
#else
                strcmp(value, "size=2048") && strcmp(value, "size=1536")
#endif
            )
                return 0;
            memory++;
        }
        if (!strcmp(arg, "--initrd")) {
            char expected_path[256];
            snprintf(expected_path, sizeof(expected_path), "%s/android-lowram.cpio", profile);
            if (strcmp(value, expected_path))
                return 0;
            initrd++;
        }
        if (!strcmp(arg, "--block")) {
            const char *files[] = {"android-work.qcow2", "android-misc.raw", "android-frp.raw"};
            char expected_path[256];
            int found = 0;
            for (size_t i = 0; i < 3; i++) {
                snprintf(expected_path, sizeof(expected_path), "path=%s/%s,ro=false", profile,
                         files[i]);
                found |= !strcmp(value, expected_path);
            }
            if (!found)
                return 0;
            disks++;
        }
#endif
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
    return named && protected && no_balloon && no_sandbox
#ifdef PLB_ANDROID_SHARED_BACKING_PROBE
           && gpu == 1 && disks == 3 && vsock == 1 && memory == 1 && initrd == 1
#endif
        ;
}

__attribute__((constructor)) static void initialize(void) {
    const char *mode = getenv(
#ifdef PLB_ANDROID_LARGE_BACKING
        "PLB_ANDROID_LARGE_BACKING"
#elif defined(PLB_ANDROID_SHARED_BACKING_PROBE)
        "PLB_ANDROID_BACKING_PROBE"
#else
        "PLB_DISKLESS_BACKING_PROBE"
#endif
    );
    struct stat actual, expected;
    if (!mode)
        return;
#ifdef PLB_ANDROID_SHARED_BACKING_PROBE
#ifdef PLB_PREFAULT_ONLY_BACKING
    if (strcmp(mode, "shared-middle-prefault"))
#else
    if (strcmp(mode, "shared-middle-refault"))
#endif
        _exit(79);
#endif
    if ((strcmp(mode, "anonymous") && strcmp(mode, "anonymous-prefault") &&
         strcmp(mode, "shared-forward") && strcmp(mode, "shared-reverse") &&
         strcmp(mode, "shared-upper-huge") && strcmp(mode, "shared-upper-refault") &&
         strcmp(mode, "shared-upper-collapse") && strcmp(mode, "shared-middle-refault") &&
         strcmp(mode, "shared-upper-locked") && strcmp(mode, "shared-middle-reverse-refault") &&
         strcmp(mode, "shared-middle-prefault")) ||
        getuid() || stat("/proc/self/exe", &actual) ||
        stat(
#ifdef PLB_ANDROID_LARGE_BACKING
            "/data/user/0/net.ryuya_dev.protectedlinux/files/android-runtime/crosvm",
#else
            "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/crosvm-copy",
#endif
            &expected) ||
        actual.st_dev != expected.st_dev || actual.st_ino != expected.st_ino ||
        !approved_arguments())
        _exit(79);
    if (!plb_host_memory_healthy())
        _exit(90);
    enabled = 1;
    lock_backing = !strcmp(mode, "shared-upper-locked");
    tail_safe = !strcmp(mode, "shared-middle-refault") ||
                !strcmp(mode, "shared-middle-reverse-refault") ||
                !strcmp(mode, "shared-middle-prefault");
    collapse_backing = !strcmp(mode, "shared-upper-collapse");
    refault_small =
        lock_backing || tail_safe || collapse_backing || !strcmp(mode, "shared-upper-refault");
    boot_safe = refault_small || !strcmp(mode, "shared-upper-huge");
    shared_backing =
        boot_safe || !strcmp(mode, "shared-forward") || !strcmp(mode, "shared-reverse");
    reverse_order = collapse_backing || !strcmp(mode, "shared-reverse") ||
                    !strcmp(mode, "shared-middle-reverse-refault");
    prefault = shared_backing || !strcmp(mode, "anonymous-prefault");
    /* Do not propagate this experiment through a later exec. */
    unsetenv("LD_PRELOAD");
    unsetenv("PLB_DISKLESS_BACKING_PROBE");
    unsetenv("PLB_ANDROID_BACKING_PROBE");
    unsetenv("PLB_ANDROID_LARGE_BACKING");
}

/* Before donation only. These are migration-racy snapshots of this mapping,
 * not pins, compound-folio measurements or hypervisor allocator statistics. */
static int sample_order(void *mapping, size_t size) {
    int input = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    if (input < 0) {
        dprintf(STDERR_FILENO, "PLB_BACKING_ORDER_UNAVAILABLE\n");
        return 0;
    }
    uint64_t entries[512];
    size_t pages = size / 4096;
    struct plb_backing_order summary = {0};
    struct plb_page_summary folios = {0};
#ifndef PLB_ANDROID_LARGE_BACKING
    struct plb_page_summary bands[24] = {0};
    const size_t band_pages = 256UL * 1024 * 1024 / 4096;
#endif
    int flags_fd = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    for (size_t at = 0; at < pages; at += 512) {
        size_t count = pages - at < 512 ? pages - at : 512;
        off_t offset = (off_t)(((uintptr_t)mapping / 4096 + at) * 8);
        if (pread(input, entries, count * 8, offset) != (ssize_t)(count * 8)) {
            close(input);
            if (flags_fd >= 0)
                close(flags_fd);
            dprintf(STDERR_FILENO, "PLB_BACKING_ORDER_UNAVAILABLE\n");
            return 0;
        }
        for (size_t i = 0; i < count; i++) {
            plb_backing_order_observe(&summary, entries[i]);
            uint64_t pfn = entries[i] & ((1ULL << 55) - 1), flags = 0;
            int present = !!(entries[i] & (1ULL << 63));
            int valid = present && pfn && flags_fd >= 0 &&
                        pread(flags_fd, &flags, 8, (off_t)(pfn * 8)) == 8;
            plb_page_observe(&folios, present, pfn != 0, pfn, valid, flags);
#ifndef PLB_ANDROID_LARGE_BACKING
            size_t band = (at + i) / band_pages;
            if (band < 24)
                plb_page_observe(&bands[band], present, pfn != 0, pfn, valid, flags);
#endif
        }
    }
    close(input);
    if (flags_fd >= 0)
        close(flags_fd);
    dprintf(STDERR_FILENO,
            "PLB_BACKING_ORDER sampled=%zu present=%zu hidden=%zu forward_run=%zu reverse_run=%zu "
            "aligned_16k_forward=%zu aligned_64k_forward=%zu\n",
            summary.samples, summary.present, summary.hidden, summary.longest_forward,
            summary.longest_reverse, summary.aligned_16k_forward, summary.aligned_64k_forward);
    dprintf(STDERR_FILENO,
            "PLB_BACKING_FOLIOS sampled=%zu present=%zu thp=%zu heads=%zu tails=%zu "
            "flag_errors=%zu alignment_2m=%d sampled_2m_folios=%zu\n",
            folios.samples, folios.present, folios.thp_pages, folios.compound_heads,
            folios.compound_tails, folios.flag_errors,
            !((uintptr_t)mapping & ((2UL * 1024 * 1024) - 1)),
            ((uintptr_t)mapping & ((2UL * 1024 * 1024) - 1)) ? 0 : folios.sampled_2m_folios);
#ifndef PLB_ANDROID_LARGE_BACKING
    for (size_t band = 0; band < 24 && bands[band].samples; band++)
        dprintf(STDERR_FILENO,
                "PLB_BACKING_BAND offset_mib=%zu sampled=%zu present=%zu thp=%zu heads=%zu\n",
                band * 256, bands[band].samples, bands[band].present, bands[band].thp_pages,
                bands[band].compound_heads);
#endif
    return plb_page_all_thp(&folios, pages);
}

static long long monotonic_millis(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        _exit(84);
    return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

static void collapse_owned_backing(void *mapping, size_t size) {
    const size_t chunk = 2UL * 1024 * 1024;
    if ((uintptr_t)mapping % chunk || size % chunk)
        _exit(85);
    long long start = monotonic_millis();
    size_t attempted = 0, succeeded = 0, no_memory = 0, other = 0;
    /* Guest allocations often start high. Work downward, stop starting new
     * calls after six seconds, and retain ordinary backing on any failure.
     * A single kernel call is additionally supervised by the VM watchdog. */
    for (size_t left = size; left && monotonic_millis() - start < 6000; left -= chunk) {
        errno = 0;
        int result =
            (int)syscall(SYS_madvise, (char *)mapping + left - chunk, chunk, MADV_COLLAPSE);
        attempted++;
        if (!result)
            succeeded++;
        else if (errno == ENOMEM)
            no_memory++;
        else
            other++;
    }
    dprintf(STDERR_FILENO,
            "PLB_BACKING_COLLAPSE attempted=%zu returned_zero=%zu enomem=%zu other=%zu "
            "elapsed_ms=%lld\n",
            attempted, succeeded, no_memory, other, monotonic_millis() - start);
}

/* Only the observed main guest memfd is split. Keep the boot/DTB region at
 * base-page granularity; the rest remains the same shared FD and offsets.
 * No ownership/protection ioctl is intercepted or weakened. */
int madvise(void *address, size_t size, int advice) {
    if (boot_safe && boot_safe_base && advice == MADV_HUGEPAGE && address == boot_safe_base &&
        size == boot_safe_size) {
        int low = (int)syscall(SYS_madvise, address, BOOT_BASE_PAGES, MADV_NOHUGEPAGE);
        int high = (int)syscall(SYS_madvise, (char *)address + BOOT_BASE_PAGES,
                                size - BOOT_BASE_PAGES - BOOT_TAIL_PAGES, MADV_HUGEPAGE);
        int tail = tail_safe ? (int)syscall(SYS_madvise, (char *)address + size - BOOT_TAIL_PAGES,
                                            BOOT_TAIL_PAGES, MADV_NOHUGEPAGE)
                             : 0;
        dprintf(STDERR_FILENO,
                "PLB_BACKING_BOOT_GUARD prefix_mib=64 tail_mib=%zu low_result=%d high_result=%d "
                "tail_result=%d\n",
                (size_t)BOOT_TAIL_PAGES / (1024 * 1024), low, high, tail);
        return low ? low : high ? high : tail;
    }
    return (int)syscall(SYS_madvise, address, size, advice);
}

static void *map_memory(void *address, size_t size, int prot, int flags, int fd, off_t offset) {
    int replace = 0;
    if (enabled && fd >= 0 && offset == 0 && size >= 128UL * 1024 * 1024 &&
        size <= 5120UL * 1024 * 1024 && (flags & MAP_TYPE) == MAP_SHARED &&
        prot == (PROT_READ | PROT_WRITE)) {
        char path[64], name[128];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
        ssize_t length = readlink(path, name, sizeof(name) - 1);
        if (length > 0 && length < (ssize_t)sizeof(name) - 1) {
            name[length] = 0;
            replace = !strcmp(name, "/memfd:crosvm_guest (deleted)");
        }
    }
#ifdef PLB_ANDROID_LARGE_BACKING
    if (replace && size != PLB_LARGE_BACKING_BYTES)
        _exit(79);
#elif defined(PLB_ANDROID_GPU_BACKING)
    if (replace && size != PLB_GPU_MEMORY_BACKING_BYTES)
        _exit(79);
#endif
    if (replace && !shared_backing) {
        flags = (flags & ~MAP_SHARED) | MAP_PRIVATE | MAP_ANONYMOUS;
        fd = -1;
        offset = 0;
    }
    void *mapping = (void *)syscall(SYS_mmap, address, size, prot, flags, fd, offset);
    if (replace && mapping != MAP_FAILED) {
        if (lock_backing) {
            /* Process-local on-fault locking only. Distinguish swap loss during
             * preparation from Gunyah mapping failure; no host policy change. */
            errno = 0;
            int locked = (int)syscall(SYS_mlock2, mapping, size, 1 /* MLOCK_ONFAULT */);
            dprintf(STDERR_FILENO, "PLB_BACKING_LOCK_ONFAULT result=%d errno=%d\n", locked, errno);
            if (locked)
                _exit(88);
        }
        if (boot_safe) {
            if (boot_safe_base)
                _exit(80);
            boot_safe_base = mapping;
            boot_safe_size = size;
            if (madvise(mapping, size, MADV_HUGEPAGE))
                _exit(81);
        }
        int advice = shared_backing ? 0 : madvise(mapping, size, MADV_HUGEPAGE);
        dprintf(STDERR_FILENO, "%s size=%zu hugepage_advice=%d\n",
                shared_backing ? "PLB_BACKING_SHARED" : "PLB_BACKING_REPLACED", size, advice);
        if (prefault) {
            /* Preserve existing bytes, including any data loaded by the VMM.
             * Shared mode retains the original FD, seals and MAP_SHARED contract.
             * Fault order is the only difference between the two shared modes. */
            volatile unsigned char *bytes = mapping;
            if (tail_safe) {
                /* Populate base-page boot margins before middle folios consume
                 * available blocks. Preserve bytes and never modify a live VM. */
                for (size_t at = 0; at < BOOT_BASE_PAGES; at += 4096) {
                    unsigned char value = bytes[at];
                    bytes[at] = value;
                }
                for (size_t at = size - BOOT_TAIL_PAGES; at < size; at += 4096) {
                    unsigned char value = bytes[at];
                    bytes[at] = value;
                }
                dprintf(STDERR_FILENO, "PLB_BACKING_BOOT_MARGINS_PREFAULTED mib=128\n");
            }
            if (collapse_backing) {
                /* Seed just one base page per PMD before allocating smaller
                 * folios. Otherwise mTHP population can consume the same
                 * scarce high-order blocks that collapse needs later. */
                void *upper = (char *)mapping + BOOT_BASE_PAGES;
                size_t upper_size = size - BOOT_BASE_PAGES - BOOT_TAIL_PAGES;
                if (madvise(upper, upper_size, MADV_NOHUGEPAGE))
                    _exit(86);
                for (size_t at = BOOT_BASE_PAGES; at < size; at += 2UL * 1024 * 1024) {
                    unsigned char value = bytes[at];
                    bytes[at] = value;
                }
                if (madvise(upper, upper_size, MADV_HUGEPAGE))
                    _exit(87);
                dprintf(STDERR_FILENO, "PLB_BACKING_SPARSE_SEED stride_mib=2 prefix_mib=64\n");
                collapse_owned_backing(upper, upper_size);
                sample_order(upper, upper_size);
            }
            size_t first_page = boot_safe ? BOOT_BASE_PAGES / 4096 : 0;
            size_t page_count = (size - BOOT_TAIL_PAGES) / 4096 - first_page;
            for (size_t page = 0; page < page_count; page++) {
                size_t at =
                    plb_backing_page_index(first_page, page_count, page, reverse_order) * 4096;
                unsigned char value = bytes[at];
                bytes[at] = value;
            }
            dprintf(STDERR_FILENO,
                    "PLB_BACKING_PREFAULTED size=%zu alignment=%zu shared=%d reverse=%d\n", size,
                    (size_t)((uintptr_t)mapping & ((2UL * 1024 * 1024) - 1)), shared_backing,
                    reverse_order);
            int suitable =
                sample_order(boot_safe ? (char *)mapping + BOOT_BASE_PAGES : mapping,
                             boot_safe ? size - BOOT_BASE_PAGES - BOOT_TAIL_PAGES : size);
#ifdef PLB_PREFAULT_ONLY_BACKING
            /* Do not hole-punch a mapped shmem folio. The diagnostic reverse
             * comparison exposed a host page-cache accounting failure. */
            if (!suitable) {
                dprintf(STDERR_FILENO, "ANDROID_BACKING_UNSUITABLE\n");
                _exit(89);
            }
            dprintf(STDERR_FILENO, "ANDROID_BACKING_ADMITTED prefault_only=1\n");
#else
            (void)suitable;
            if (refault_small) {
                void *upper = (char *)mapping + BOOT_BASE_PAGES;
                size_t upper_size = size - BOOT_BASE_PAGES - BOOT_TAIL_PAGES;
                if (plb_refault_owned_shmem(fd, upper, upper_size, BOOT_BASE_PAGES, 4))
                    _exit(82);
            }
#endif
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
