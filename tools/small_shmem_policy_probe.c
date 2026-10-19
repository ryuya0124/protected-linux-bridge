/* MIT. Development-only runtime-policy trial; not included in the APK.
 * --plan is read-only. --apply-approved-small-shmem must not be run without explicit human
 * authorization for the three small-shmem host-wide settings documented in the proposal.
 * PLB_MEDIUM_SHMEM_PROPOSAL is a separate six-key proposal, with a different
 * apply token. Existing three-key approval never authorizes that token.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/file.h>
#include <sys/system_properties.h>
#include <linux/magic.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#define PLB_OWNER_LOCK_FILE FILES "/vm-owner.lock"
#include "../native/profile_lock.h"
#include "host_memory_health.h"
#if defined(PLB_ANDROID4G_CUSTOM_GUEST) && !defined(PLB_PREALLOCATED_VM_POLICY)
#error "Custom guest kernel requires the isolated adopted-RAM policy diagnostic"
#endif
#ifdef PLB_PREALLOCATED_VM_POLICY
#include "preallocated_vm_limits.h"
#include "android4g_config.h"
#include "preallocated_ready.h"
#ifdef PLB_ANDROID4G_INTERACTIVE
#include "diagnostic_owner_lock.h"
#endif
#endif
#include "../native/android_owned_process.h"
#ifdef PLB_GPU_WARNING_ONCE
#include "gpu_warning_once.h"
#include "android_gpu_memory_config.h"
#endif

#ifdef PLB_POLICY_MOCK
#define APPLY_OPTION "--run-mock"
static const char *settings[] = {
    FILES "/kernel-probes/policy-mock/small-16k",
    FILES "/kernel-probes/policy-mock/small-32k",
    FILES "/kernel-probes/policy-mock/small-64k",
};
static const char *allocation_probe = FILES "/kernel-probes/policy-mock/child";
#elif defined(PLB_MEDIUM_SHMEM_PROPOSAL)
#ifndef PLB_PREALLOCATED_VM_POLICY
#error "The medium proposal requires the separate owned-FD diagnostic mode"
#endif
#define APPLY_OPTION "--apply-approved-medium-shmem"
static const char *settings[] = {
    "/sys/kernel/mm/transparent_hugepage/hugepages-16kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-32kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-64kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-128kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-256kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-512kB/shmem_enabled",
};
#ifdef PLB_ANDROID4G_INTERACTIVE
static const char *allocation_probe = PLB_ANDROID4G_RUNTIME "/allocation-probe";
#else
static const char *allocation_probe = FILES "/kernel-probes/backing-allocation-probe";
#endif
#else
#define APPLY_OPTION "--apply-approved-small-shmem"
static const char *settings[] = {
    "/sys/kernel/mm/transparent_hugepage/hugepages-16kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-32kB/shmem_enabled",
    "/sys/kernel/mm/transparent_hugepage/hugepages-64kB/shmem_enabled",
};
static const char *allocation_probe = FILES "/kernel-probes/backing-allocation-probe";
#endif
#define SETTING_COUNT ((int)(sizeof(settings) / sizeof(settings[0])))
#if defined(PLB_ANDROID4G_MEDIUM_ONLY) && !defined(PLB_MEDIUM_SHMEM_PROPOSAL)
#error "The narrowed Android comparison requires the approved six-key guardian"
#endif
static const char *trial_mode = "advise";
static const char *proposed_mode(int index, int only_64k) {
#ifdef PLB_ANDROID4G_MEDIUM_ONLY
    /* Leave the three small sizes at their original never; do not enable a new node. */
    if (index < 3)
        return "never";
#endif
    return only_64k && index != 2 ? "never" : trial_mode;
}
static const char *vm_probe = FILES "/kernel-probes/memory-runner";
struct vm_stage {
    const char *option, *memory, *touch, *backing;
    long headroom_mib;
};
static const struct vm_stage vm_stages[] = {
    {"--vm-512-256", "512", "256", "0", 1280},
    {"--vm-1024-768", "1024", "768", "0", 1792},
    {"--vm-512-256-boot-safe", "512", "256", "5", 1280},
    {"--vm-1024-768-boot-safe", "1024", "768", "5", 1792},
    {"--vm-512-256-boot-safe-refault", "512", "256", "6", 1280},
    {"--vm-1024-768-boot-safe-refault", "1024", "768", "6", 1792},
    {"--vm-2048-1536-boot-safe-refault", "2048", "1536", "6", 2816},
    {"--vm-4096-3072-boot-safe-refault", "4096", "3072", "6", 4864},
    {"--vm-5120-4096-boot-safe-refault", "5120", "4096", "6", 5888},
    {"--vm-512-256-boot-safe-refault-collapse", "512", "256", "7", 1280},
    {"--vm-2048-1536-boot-safe-refault-collapse", "2048", "1536", "7", 2816},
    {"--vm-4096-3072-boot-safe-refault-collapse", "4096", "3072", "7", 4864},
    {"--vm-5120-4096-boot-safe-refault-collapse", "5120", "4096", "7", 5888},
    {"--vm-5120-4096-boot-safe-refault-locked", "5120", "4096", "8", 5888},
    {"--vm-512-256-boot-safe-refault-locked", "512", "256", "8", 1280},
    {"--vm-512-256-boot-safe-refault-middle", "512", "256", "9", 1280},
    {"--vm-2048-1536-boot-safe-refault-middle", "2048", "1536", "9", 2816},
    {"--vm-3072-2048-boot-safe-refault-middle", "3072", "2048", "9", 3840},
    {"--vm-4480-4096-boot-safe-refault-middle", "4480", "4096", "9", 5248},
    {"--vm-512-256-boot-safe-refault-middle-reverse", "512", "256", "10", 1280},
    {"--vm-3072-2048-boot-safe-refault-middle-reverse", "3072", "2048", "10", 3840},
    {"--vm-4480-4096-boot-safe-refault-middle-reverse", "4480", "4096", "10", 5248},
};
static volatile sig_atomic_t interrupted;
static void stop(int number) {
    (void)number;
    interrupted = 1;
}

static int read_mode(const char *path, char value[32]) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    char buffer[256];
    if (fd < 0)
        return -1;
    ssize_t size = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (size <= 0)
        return -1;
    buffer[size] = 0;
    char *begin = strchr(buffer, '['), *end;
    if (!begin) {
#ifdef PLB_POLICY_MOCK
        if (!strcmp(buffer, "never") || !strcmp(buffer, "advise") || !strcmp(buffer, "always")) {
            strcpy(value, buffer);
            return 0;
        }
#endif
        return -1;
    }
    if (!(end = strchr(++begin, ']')) || end - begin >= 32)
        return -1;
    memcpy(value, begin, (size_t)(end - begin));
    value[end - begin] = 0;
    return 0;
}

static int write_mode(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    struct statfs fs;
    struct stat st;
    int valid = !fstat(fd, &st) && S_ISREG(st.st_mode) && !st.st_uid;
#ifdef PLB_POLICY_MOCK
    (void)fs;
    valid = valid && !ftruncate(fd, 0);
#else
    valid = valid && !fstatfs(fd, &fs) && fs.f_type == SYSFS_MAGIC;
#endif
    size_t size = strlen(value);
    int result = valid && write(fd, value, size) == (ssize_t)size ? 0 : -1;
    close(fd);
    char actual[32];
    if (result || read_mode(path, actual) || strcmp(actual, value))
        return -1;
    return 0;
}

static int restore(void) {
    int failed = 0;
    for (int i = SETTING_COUNT - 1; i >= 0; i--) {
        char actual[32];
        if (read_mode(settings[i], actual)) {
            failed = 1;
        } else if (!strcmp(actual, trial_mode)) {
            if (write_mode(settings[i], "never"))
                failed = 1;
        } else if (strcmp(actual, "never")) {
            /* Do not clobber an unrelated concurrent administrator change. */
            failed = 1;
        }
    }
    return failed;
}

static pid_t sample(pid_t child, int *status) {
#ifdef PLB_GPU_WARNING_ONCE
    if (plb_gpu_once_kernel_bad())
        interrupted = 1;
#endif
    pid_t result;
    do {
        result = waitpid(child, status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    return result;
}

static int unchanged_host(const char *boot) {
    char current[80] = {0}, selinux[2] = {0};
    int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ssize_t size = read(fd, current, sizeof(current) - 1);
    close(fd);
    fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ssize_t enforce = read(fd, selinux, 1);
    close(fd);
    return size > 0 && enforce == 1 && selinux[0] == '1' && !strcmp(current, boot);
}

static long available_kib(void) {
    FILE *input = fopen("/proc/meminfo", "re");
    char line[256];
    long available = -1;
    if (!input)
        return -1;
    while (fgets(line, sizeof(line), input))
        if (sscanf(line, "MemAvailable: %ld kB", &available) == 1)
            break;
    fclose(input);
    return available;
}

#ifdef PLB_PREALLOCATED_VM_POLICY
static int prepare_owned_memory(int lock) {
    const char *path =
#ifdef PLB_ANDROID4G_INTERACTIVE
        PLB_ANDROID4G_RUNTIME "/pressure-check";
#else
        FILES "/kernel-probes/preallocated-pressure-check";
#endif
    struct stat info;
    if (lstat(path, &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink != 1 ||
        info.st_mode & 077) {
        return 45;
    }
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) {
            _exit(46);
        }
        char descriptor[16];
        snprintf(descriptor, sizeof(descriptor), "%d", lock);
        if (fcntl(lock, F_SETFD, 0) || setenv("PLB_DIAGNOSTIC_OWNER_FD", descriptor, 1)) {
            _exit(46);
        }
        execl(path, path, "detached", "reclaim-6g", (char *)NULL);
        _exit(47);
    }
    if (child < 0) {
        return 48;
    }
    int status = 0;
    for (int i = 0; i < 320; i++) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 49;
        }
        if ((done < 0 && errno != EINTR) || !plb_host_memory_healthy()) {
            break;
        }
        usleep(100000);
    }
    kill(child, SIGKILL); /* Still unreaped direct child; never another task. */
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return 124;
}
#endif

int main(int argc, char **argv) {
    char model[PROP_VALUE_MAX], sdk[PROP_VALUE_MAX];
#ifdef PLB_ANDROID4G_INTERACTIVE
    if (argc != 5 || strcmp(argv[1], "--apply-approved-medium-shmem") ||
        strcmp(argv[2], "--android4g") || strcmp(argv[3], "--prepare-memory") ||
        strlen(argv[4]) != 5 || strspn(argv[4], "0123456789") != 5 ||
        strtoul(argv[4], NULL, 10) < 10000 || strtoul(argv[4], NULL, 10) > 19999)
        return 20;
    const char *interactive_uid = argv[4];
    uid_t app_uid = (uid_t)strtoul(interactive_uid, NULL, 10);
    argc = 4;
#endif
    int prepare_memory = 0;
    int full_content = 0;
    int only_64k = 0;
#ifdef PLB_PREALLOCATED_VM_POLICY
    if (argc >= 4 && argc <= 6) {
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--prepare-memory") && !prepare_memory)
                prepare_memory = 1;
            else if (!strcmp(argv[i], "--full-content") && !full_content)
                full_content = 1;
            else if (!strcmp(argv[i], "--64k-only") && !only_64k)
                only_64k = 1;
            else
                return 20;
        }
        argc = 3;
    }
#endif
    if (getuid() || (argc != 2 && argc != 3) ||
        (strcmp(argv[1], "--plan") && strcmp(argv[1], APPLY_OPTION)))
        return 20;
    const struct vm_stage *stage = NULL;
    if (argc == 3)
        for (size_t i = 0; i < sizeof(vm_stages) / sizeof(vm_stages[0]); i++)
            if (!strcmp(argv[2], vm_stages[i].option))
                stage = &vm_stages[i];
    int android1536 = argc == 3 && !strcmp(argv[2], "--android-1536");
    int android_gpu1536 = argc == 3 && !strcmp(argv[2], "--android-gpu1536-gles");
    int android =
        android1536 || android_gpu1536 || (argc == 3 && !strcmp(argv[2], "--android-2048"));
    int vm = stage != NULL || android;
    int refault = argc == 3 && !strcmp(argv[2], "--allocation-refault");
    int preallocated = 0;
    int adopted_android = 0;
    int preallocated_memory = 0, preallocated_touch = 0;
    int preallocated_chunked = 0;
#ifdef PLB_PREALLOCATED_VM_POLICY
    if (argc == 3 && !strcmp(argv[2], "--android4g")) {
        preallocated = 1;
        preallocated_memory = 4096;
        adopted_android = 1;
        vm = 1;
    } else if (argc == 3 && !strcmp(argv[2], "--preallocated-16")) {
        preallocated = 1;
    } else if (argc == 3) {
        if (plb_preallocated_policy(argv[2], &preallocated_memory, &preallocated_touch)) {
            preallocated = 1;
            vm = 1;
        } else if (plb_preallocated_policy_mode(argv[2], &preallocated_memory, &preallocated_touch,
                                                1)) {
            preallocated = 1;
            preallocated_chunked = 1;
            vm = 1;
        } else if (plb_preallocated_policy_mode(argv[2], &preallocated_memory, &preallocated_touch,
                                                2)) {
            preallocated = 1;
            preallocated_chunked = 2;
            vm = 1;
        } else if (plb_preallocated_policy_mode(argv[2], &preallocated_memory, &preallocated_touch,
                                                3)) {
            preallocated = 1;
            preallocated_chunked = 3;
            vm = 1;
        } else if (plb_preallocated_policy_mode(argv[2], &preallocated_memory, &preallocated_touch,
                                                4)) {
            preallocated = 1;
            preallocated_chunked = 4;
            vm = 1;
        }
    }
#endif
    if (adopted_android && (full_content || only_64k))
        return 20;
    if (prepare_memory && (!preallocated || !strcmp(argv[1], "--plan")))
        return 20;
    if (full_content && (!preallocated || !preallocated_memory || !strcmp(argv[1], "--plan")))
        return 20;
    if (only_64k && (!preallocated || !strcmp(argv[1], "--plan")))
        return 20;
#ifdef PLB_MEDIUM_SHMEM_PROPOSAL
    if (only_64k || (strcmp(argv[1], "--plan") && !preallocated))
        return 20;
#endif
    if ((argc == 3 && !vm && !refault && !preallocated) ||
        (!strcmp(argv[1], "--plan") && argc != 2))
        return 20;
    const char *probe = android_gpu1536 ? FILES "/kernel-probes/"
#ifdef PLB_GPU_WARNING_ONCE
                            PLB_GPU_MEMORY_RUNNER
#else
                                                "android-gpu1536-gles-runner"
#endif
                        : android1536 ? FILES "/kernel-probes/android-memory1536-policy-runner"
                        : android     ? FILES "/kernel-probes/android-memory-policy-runner"
                        : vm          ? vm_probe
                                      : allocation_probe;
    if (preallocated)
        probe = adopted_android       ? PLB_ANDROID4G_MANAGER
                : full_content        ? FILES "/kernel-probes/preallocated-vm-full-probe"
                : preallocated_memory ? FILES "/kernel-probes/preallocated-vm-probe"
                                      : FILES "/kernel-probes/preallocated-ram-probe";
    /* Android retains its preallocated memory after policy restoration. Only
     * the first ten seconds use the approved host policy; observe boot later. */
    int deferred = android || (preallocated && preallocated_memory);
    int controller_steps = preallocated_memory ? 300 : deferred ? 100 : vm ? 320 : 120;
    int guardian_ms = vm ? 40000 : 20000;
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.version.sdk", sdk);
    if (strcmp(model, "SM-S948Q") || strcmp(sdk, "36"))
        return 21;
    if (strcmp(argv[1], "--plan") &&
#ifdef PLB_GPU_WARNING_ONCE
        (!android_gpu1536 || !plb_gpu_once_window())
#else
        !plb_host_memory_healthy()
#endif
    ) {
        fprintf(stderr, "PLB_POLICY_REJECTED host_memory_warning=1\n");
        return 38;
    }
    if (((android && !android_gpu1536) || (stage && atoi(stage->backing) >= 6)) &&
        strcmp(argv[1], "--plan")) {
        fprintf(stderr, "PLB_POLICY_REJECTED mapped_refault_retired=1\n");
        return 39;
    }
    char boot[80] = {0};
    int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 22;
    ssize_t size = read(fd, boot, sizeof(boot) - 1);
    close(fd);
    if (size <= 0 || !unchanged_host(boot))
        return 22;
    for (int i = 0; i < SETTING_COUNT; i++) {
        char mode[32];
        if (read_mode(settings[i], mode) || strcmp(mode, "never"))
            return 23;
        printf("PLB_POLICY_PLAN index=%d original=never proposed=%s\n", i,
               proposed_mode(i, only_64k));
    }
    if (!strcmp(argv[1], "--plan"))
        return 0;
    if (available_kib() < (prepare_memory ? 1600L
                           : preallocated ? 1536L + (preallocated_memory ? preallocated_memory : 16)
                           : (android1536 || android_gpu1536) ? 2304L
                           : android                          ? 2816L
                           : vm                               ? stage->headroom_mib
                                                              : 512L) *
                              1024)
        return 37;
    struct stat executable;
    if (lstat(probe, &executable) || !S_ISREG(executable.st_mode) || executable.st_uid ||
        executable.st_nlink != 1 || executable.st_mode & 077)
        return 24;
    int lock =
#ifdef PLB_ANDROID4G_INTERACTIVE
        diagnostic_owner_lock();
#else
        profile_lock();
#endif
    if (lock < 0)
        return 28;
#ifdef PLB_PREALLOCATED_VM_POLICY
#ifdef PLB_ANDROID4G_INTERACTIVE
    if (android_isolate_owned_controller(app_uid))
        return 41;
#endif
    if (prepare_memory) {
        /* Keep one owner across pressure, release, preparation and VM. The
         * policy guardian starts afterwards; no policy is changed during this. */
        int prepared = prepare_owned_memory(lock);
        if (prepared || !plb_host_memory_healthy() ||
            available_kib() < (1536L + (preallocated_memory ? preallocated_memory : 16)) * 1024) {
            close(lock);
            return prepared ? prepared : 50;
        }
        puts("PLB_PREALLOCATED_ATOMIC_PREPARATION_COMPLETE");
    }
#endif
    if (android_gpu1536 || adopted_android) {
        /* A developer launch must already be outside an app freezer group.
         * With uid zero this helper accepts only the existing root group and
         * refuses an app group; it never moves another task or changes policy. */
        if (android_isolate_owned_controller(
#ifdef PLB_ANDROID4G_INTERACTIVE
                app_uid
#else
                0
#endif
                ))
            return 41;
#ifdef PLB_GPU_WARNING_ONCE
        if (plb_gpu_once_watch_open())
            return 43;
        if (plb_gpu_once_claim() < 0)
            return 44;
        fprintf(stderr, "PLB_GPU_ONCE_CLAIMED memory_mib=1536 no_retry=1\n");
#endif
        pid_t parent = getpid(), validator = fork();
        if (!validator) {
            if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
                _exit(31);
            execl(probe, probe, "--check", (char *)NULL);
            _exit(32);
        }
        if (validator < 0)
            return 26;
        int status = 0;
        pid_t done = 0;
        for (int i = 0; i < 100 && !interrupted; i++) {
            done = sample(validator, &status);
            if (done)
                break;
            usleep(100000);
        }
        if (!done) {
            kill(validator, SIGKILL);
            while (waitpid(validator, NULL, 0) < 0 && errno == EINTR) {
            }
            return 124;
        }
        if (done < 0 || !WIFEXITED(status) || WEXITSTATUS(status))
            return 42;
    }
#ifdef PLB_GPU_WARNING_ONCE
    if (plb_gpu_once_kernel_bad() || interrupted)
        return 43;
#endif
    int alive[2];
    if (pipe2(alive, O_CLOEXEC))
        return 25;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGCHLD, SIG_DFL);
    fflush(NULL);
    pid_t controller = getpid();
    pid_t guardian = fork();
    if (!guardian) {
        close(alive[1]);
        /* Survive controller/session loss; never adopt a VM or another PID. */
        setsid();
        struct pollfd watching = {.fd = alive[0], .events = POLLIN | POLLHUP};
        int ready = poll(&watching, 1, guardian_ms);
        if (ready <= 0 && getppid() == controller)
            kill(controller, SIGKILL); /* Owned controller only, before restoration. */
        int failed = restore();
        dprintf(STDERR_FILENO, "PLB_POLICY_GUARDIAN_RESTORED=%d\n", !failed);
        close(alive[0]);
        close(lock);
        _exit(failed ? 30 : 0);
    }
    close(alive[0]);
    if (guardian < 0) {
        close(alive[1]);
        return 26;
    }
    int result = 0;
    int preparation_finished = 0;
    pid_t child = -1;
    int preparation[2] = {-1, -1};
    if (preallocated_memory && pipe2(preparation, O_CLOEXEC | O_NONBLOCK))
        result = 51;
    for (int i = 0; i < SETTING_COUNT && !result; i++)
        if (write_mode(settings[i], proposed_mode(i, only_64k)))
            result = 27;
    if (!result) {
#ifdef PLB_POLICY_MOCK_KILL
        kill(getpid(), SIGKILL);
#endif
        pid_t parent = getpid();
        child = fork();
        if (!child) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent)
                _exit(31);
            /* Reopen would conflict with this supervisor's lock. Pass the
             * same open description, checked by diagnostic_owner_lock(). */
            char descriptor[16];
            snprintf(descriptor, sizeof(descriptor), "%d", lock);
            if (fcntl(lock, F_SETFD, 0) || setenv("PLB_DIAGNOSTIC_OWNER_FD", descriptor, 1))
                _exit(36);
            if (preparation[1] >= 0) {
                close(preparation[0]);
                snprintf(descriptor, sizeof(descriptor), "%d", preparation[1]);
                if (fcntl(preparation[1], F_SETFD, 0) ||
                    setenv("PLB_PREALLOCATED_READY_FD", descriptor, 1))
                    _exit(52);
            }
            if (adopted_android) {
#ifdef PLB_ANDROID4G_INTERACTIVE
                execl(probe, probe, interactive_uid, (char *)NULL);
#else
                execl(probe, probe, (char *)NULL);
#endif
            } else if (preallocated && preallocated_memory) {
                char memory[16], touch[16];
                snprintf(memory, sizeof(memory), "%d", preallocated_memory);
                snprintf(touch, sizeof(touch), "%d", preallocated_touch);
                execl(probe, probe, memory, touch,
                      preallocated_chunked == 4   ? "--normal-first-collapse"
                      : preallocated_chunked == 3 ? "--normal-first-pin"
                      : preallocated_chunked == 2 ? "--normal-first"
                      : preallocated_chunked      ? "--chunked-folios"
                                                  : "--small-folios",
                      (char *)NULL);
            } else if (preallocated)
                execl(probe, probe, "--small-folios", (char *)NULL);
            else if (android)
                execl(probe, probe, (char *)NULL);
            else if (vm)
                execl(probe, probe, "linux618", stage->memory, stage->touch, "1", stage->backing,
                      "0", "1", "0", (char *)NULL);
            else
                execl(probe, probe, refault ? "shared-refault" : "shared", (char *)NULL);
            _exit(32);
        }
        if (child < 0) {
            result = 29;
        } else {
            if (preparation[1] >= 0) {
                close(preparation[1]);
                preparation[1] = -1;
            }
            int status = 0;
            pid_t done = 0;
            for (int i = 0; i < controller_steps && !interrupted; i++) {
                done = sample(child, &status);
                if (done)
                    break;
                if (preparation[0] >= 0) {
                    char ready;
                    ssize_t size = read(preparation[0], &ready, 1);
                    if (size == 1 && ready == 'R') {
                        puts("PLB_PREALLOCATED_POLICY_PREPARATION_FINISHED");
                        preparation_finished = 1;
                        break;
                    }
                    if (size == 1 || (size < 0 && errno != EAGAIN && errno != EINTR)) {
                        interrupted = 1;
                        break;
                    }
                }
                usleep(100000);
            }
            if (!done && preallocated_memory && !preparation_finished) {
                kill(child, SIGKILL);
                result = 124;
            } else if (!done && deferred && !interrupted) {
                /* Restore policy before observing the rest of Android boot. */
                result = 0;
            } else if (!done) {
                kill(child, SIGKILL);
                result = 124;
            } else {
                result = done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 33;
                child = -1; /* Reaped: never signal this numeric PID again. */
            }
        }
    }
    int failed = restore();
    fprintf(preallocated ? stderr : stdout, "PLB_POLICY_RESTORED=%d HOST_BOUNDARY_UNCHANGED=%d\n",
            !failed, unchanged_host(boot));
    if (preparation[0] >= 0)
        close(preparation[0]);
    if (preparation[1] >= 0)
        close(preparation[1]);
    close(alive[1]); /* Guardian verifies/restores, even if this process now dies. */
    if (child > 0 && !deferred) {
        int status;
        for (int i = 0; i < 20 && sample(child, &status) == 0; i++)
            usleep(100000);
    }
    int status = 0;
    pid_t guardian_done = 0;
    for (int i = 0; i < 30 && !(guardian_done = sample(guardian, &status)); i++)
        usleep(100000);
    if (deferred && child > 0 && !failed && !interrupted && guardian_done == guardian &&
        WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        int child_status = 0;
        pid_t done = 0;
        int observation_steps =
#ifdef PLB_ANDROID4G_INTERACTIVE
            18100;
#else
            adopted_android ? 1800
            : preallocated  ? 250
                            : 1300;
#endif
        for (int i = 0; i < observation_steps && !interrupted; i++) {
            done = sample(child, &child_status);
            if (done)
                break;
            usleep(100000);
        }
        if (!done) {
            kill(child, SIGKILL);
            while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
            }
            result = 124;
        } else {
            result = done > 0 && WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 33;
        }
        child = -1;
        printf("PLB_ANDROID_OBSERVATION_FINISHED result=%d HOST_BOUNDARY_UNCHANGED=%d\n", result,
               unchanged_host(boot));
    } else if (deferred && child > 0) {
        kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    close(lock);
    if (guardian_done != guardian || !WIFEXITED(status) || WEXITSTATUS(status))
        return 35;
    return failed || !unchanged_host(boot) ? 34 : result;
}
