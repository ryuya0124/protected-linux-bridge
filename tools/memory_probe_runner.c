/* MIT. Fixed-path root diagnostic: one tracked, diskless protected guest only. */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/file.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#define PLB_OWNER_LOCK_FILE FILES "/vm-owner.lock"
#include "diagnostic_owner_lock.h"
static volatile sig_atomic_t stopping;
static void stop(int signal) {
    (void)signal;
    stopping = 1;
}
static int number(const char *text) {
    char *end;
    long value = strtol(text, &end, 10);
    return *text && !*end && value >= 0 && value <= 5120 ? (int)value : -1;
}
static int safe_log(const char *path) {
    struct stat st;
    if (!lstat(path, &st))
        return S_ISREG(st.st_mode) && !st.st_uid && st.st_nlink == 1 ? unlink(path) : -1;
    return errno == ENOENT ? 0 : -1;
}

/* Only the child created below is sampled, never another app or trusted VM. */
static void sample(pid_t child, int elapsed_ms) {
    char path[96], line[512];
    snprintf(path, sizeof(path), "/proc/%ld/smaps_rollup", (long)child);
    FILE *input = fopen(path, "re");
    if (!input)
        return;
    printf("PLB_VMM_SAMPLE elapsed_ms=%d\n", elapsed_ms);
    while (fgets(line, sizeof(line), input)) {
        if (!strncmp(line, "Rss:", 4) || !strncmp(line, "Pss:", 4) ||
            !strncmp(line, "Anonymous:", 10) || !strncmp(line, "AnonHugePages:", 14) ||
            !strncmp(line, "ShmemPmdMapped:", 15) || !strncmp(line, "Locked:", 7))
            fputs(line, stdout);
    }
    fclose(input);
    snprintf(path, sizeof(path), "/proc/%ld/status", (long)child);
    input = fopen(path, "re");
    if (input) {
        while (fgets(line, sizeof(line), input))
            if (!strncmp(line, "VmSize:", 7) || !strncmp(line, "VmLck:", 6) ||
                !strncmp(line, "VmPin:", 6) || !strncmp(line, "VmRSS:", 6) ||
                !strncmp(line, "VmSwap:", 7))
                fputs(line, stdout);
        fclose(input);
    }
    if (elapsed_ms == 1000) {
        snprintf(path, sizeof(path), "/proc/%ld/limits", (long)child);
        input = fopen(path, "re");
        if (input) {
            while (fgets(line, sizeof(line), input))
                if (!strncmp(line, "Max locked memory", 17))
                    fputs(line, stdout);
            fclose(input);
        }
        snprintf(path, sizeof(path), "/proc/%ld/maps", (long)child);
        input = fopen(path, "re");
        if (input) {
            while (fgets(line, sizeof(line), input))
                if (strstr(line, "crosvm_guest"))
                    printf("PLB_GUEST_BACKING %s", line);
            fclose(input);
        }
    }
    fflush(stdout);
}

static pid_t poll_child(pid_t child, int *status) {
    pid_t result;
    do {
        result = waitpid(child, status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    return result;
}
int main(int argc, char **argv) {
    if (getuid() != 0 || (argc < 5 || argc > 9))
        return 20;
    const char *kernel = !strcmp(argv[1], "linux618")    ? FILES "/kernels/linux-6.18.57-plb1/Image"
                         : !strcmp(argv[1], "sdk37-16k") ? FILES "/kernels/android37-sdk16k/Image"
                         : !strcmp(argv[1], "cf17") ? FILES "/kernels/android17-cuttlefish/Image"
                         : !strcmp(argv[1], "stock")
                             ? "/apex/com.android.virt/etc/fs/microdroid_kernel"
                             : NULL;
    int mem = number(argv[2]), touch = number(argv[3]), huge = number(argv[4]);
    int anonymous = argc >= 6 ? number(argv[5]) : 0;
    int dump_fdt = argc >= 7 ? number(argv[6]) : 0;
    int rounds = argc >= 8 ? number(argv[7]) : 1;
    int discard = argc == 9 ? number(argv[8]) : 0;
    int prepared_large = (anonymous == 6 || anonymous == 7 || anonymous == 8) && huge == 1 &&
                         rounds == 1 && !discard && !dump_fdt &&
                         ((mem == 2048 && touch == 1536) || (mem == 4096 && touch == 3072) ||
                          (mem == 5120 && touch == 4096));
    int lazy_large = mem == 4096 && touch <= 256 && !anonymous && !huge;
    if (!kernel ||
        (mem != 512 && mem != 768 && mem != 1024 && mem != 2048 && mem != 4096 && mem != 5120) ||
        touch < 64 || touch > mem - 64 || huge < 0 || huge > 1 || anonymous < 0 || anonymous > 8 ||
        dump_fdt < 0 || dump_fdt > 1 || (dump_fdt && anonymous) || rounds < 1 || rounds > 8 ||
        touch * rounds > (prepared_large ? 4096 : 3072) || discard < 0 || discard > 1 ||
        (discard && rounds < 2) || (anonymous >= 5 && !huge) ||
        (mem >= 2048 && !prepared_large && !lazy_large))
        return 21;
    if (anonymous) {
        struct stat library, copy;
        if (lstat(FILES "/kernel-probes/libmemory_backing_probe.so", &library) ||
            !S_ISREG(library.st_mode) || library.st_uid || library.st_nlink != 1 ||
            library.st_mode & 077)
            return 29;
        if (lstat(FILES "/kernel-probes/crosvm-copy", &copy) || !S_ISREG(copy.st_mode) ||
            copy.st_uid || copy.st_nlink != 1 || copy.st_mode & (0222 | 077))
            return 29;
    }
    int lock = diagnostic_owner_lock();
    if (lock < 0)
        return 28;
    umask(077);
    if (safe_log(FILES "/kernel-probes/memory-live.log"))
        return 22;
    if (dump_fdt && safe_log(FILES "/kernel-probes/memory-last.dtb"))
        return 22;
    printf("PLB_RUNNER_START memory_mib=%d touch_mib=%d rounds=%d discard=%d\n", mem, touch, rounds,
           discard);
    fflush(stdout);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    pid_t parent = getpid(), child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(23);
        const char *backing_modes[] = {NULL,
                                       "anonymous",
                                       "anonymous-prefault",
                                       "shared-forward",
                                       "shared-reverse",
                                       "shared-upper-huge",
                                       "shared-upper-refault",
                                       "shared-upper-collapse",
                                       "shared-upper-locked"};
        if (anonymous &&
            (setenv("LD_PRELOAD", FILES "/kernel-probes/libmemory_backing_probe.so", 1) ||
             setenv("PLB_DISKLESS_BACKING_PROBE", backing_modes[anonymous], 1) ||
             setenv("LD_LIBRARY_PATH",
                    "/apex/com.android.virt/lib64:/system/lib64:/system_ext/lib64", 1)))
            _exit(27);
        /* Prefer reclaiming the diagnostic VMM over ordinary Android apps.
         * This is per-process, not a host memory-policy change. */
        int oom = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
        if (oom < 0 || write(oom, "1000", 4) != 4)
            _exit(30);
        close(oom);
        char memory[32], options[256];
        snprintf(memory, sizeof(memory), "size=%d", mem);
        snprintf(options, sizeof(options),
                 "console=ttyS0 8250.nr_uarts=1 rdinit=/init panic=0 quiet loglevel=3 "
                 "plb.pressuremib=%d plb.rounds=%d plb.discard=%d",
                 touch, rounds, discard);
        const char *args[] = {"crosvm",
                              "run",
                              "--name",
                              "plb-memory-probe",
                              "--disable-sandbox",
                              "--no-balloon",
                              "--protected-vm-without-firmware",
                              "--swiotlb",
                              "32",
                              "--mem",
                              memory,
                              "--cpus",
                              "num-cores=1",
                              "--serial",
                              "type=file,path=" FILES "/kernel-probes/memory-live.log,console=true",
                              "--params",
                              options,
                              "--initrd",
                              FILES "/kernel-probes/memory-probe.cpio",
                              kernel,
                              NULL,
                              NULL,
                              NULL,
                              NULL};
        int at = 20;
        if (huge) {
            args[at++] = "--hugepages";
        }
        if (dump_fdt) {
            args[at++] = "--dump-device-tree-blob";
            args[at++] = FILES "/kernel-probes/memory-last.dtb";
        }
        args[at] = NULL;
        execv(anonymous ? FILES "/kernel-probes/crosvm-copy" : "/apex/com.android.virt/bin/crosvm",
              (char *const *)args);
        _exit(24);
    }
    if (child < 0)
        return 25;
    int status = 0;
    pid_t done = 0;
    int probe_steps = prepared_large ? 300 : 250;
    for (int i = 0; i < probe_steps && !stopping; i++) {
        done = poll_child(child, &status);
        if (done)
            break;
        if (i % 10 == 0)
            sample(child, i * 100);
        usleep(100000);
    }
    if (!done) {
        kill(child, SIGTERM);
        for (int i = 0; i < 20 && !done; i++) {
            usleep(100000);
            done = poll_child(child, &status);
        }
        if (!done) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
        }
        return 124;
    }
    close(lock);
    return done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 26;
}
