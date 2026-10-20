/* MIT. Bounded Android 17 developer session; owned copy, protected RAM and GLES only. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#include "android17_config.generated.h"
#define BASE PLB_ANDROID4G_BASE
#include "profile_lock.h"
#include "host_memory_health.h"
#include "diagnostic_kernel_monitor.h"
#include "android4g_resource_limits.h"
#include "preallocated_ready.h"
#include "six_shmem_guard.generated.h"
#include "pins.generated.h"
#include "android_owned_process.h"
#include "android_sensor_transport.h"
static volatile _Atomic sig_atomic_t stopping;
#include "android_gpu_console.h"
#define PLB_ANDROID_ADOPTED_PROBE 1
#include "android_memory_observer.h"

static void stop(int number) {
    (void)number;
    stopping = 1;
}
static int platform(void) {
    char value[PROP_VALUE_MAX];
    struct utsname kernel;
    __system_property_get("ro.product.model", value);
    if (getuid() || strcmp(value, "SM-S948Q"))
        return 0;
    __system_property_get("ro.build.version.incremental", value);
    if (strcmp(value, "S948QOPU1BZI9"))
        return 0;
    __system_property_get("ro.build.version.sdk", value);
    if (strcmp(value, "37") || uname(&kernel) ||
        strcmp(kernel.release, "6.12.69-android16-6-pee899be-abogkiS948QOPU1BZI9-4k"))
        return 0;
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    char state = 0;
    int okay = fd >= 0 && read(fd, &state, 1) == 1 && state == '1';
    if (fd >= 0)
        close(fd);
    return okay && plb_host_memory_healthy() && plb_shmem_original();
}
static int pinned(const char *path, const char *digest) {
    struct stat info;
    if (lstat(path, &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink != 1 ||
        (info.st_mode & 077))
        return 0;
    int channel[2];
    if (pipe2(channel, O_CLOEXEC))
        return 0;
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(126);
        dup2(channel[1], STDOUT_FILENO);
        close(channel[0]);
        close(channel[1]);
        execl("/system/bin/toybox", "toybox", "sha256sum", path, (char *)NULL);
        _exit(126);
    }
    close(channel[1]);
    char text[512] = {0};
    ssize_t size = read(channel[0], text, sizeof(text));
    close(channel[0]);
    if (child < 0)
        return 0;
    int status = 0;
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR)
            return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && size > 65 &&
           !memcmp(text, digest, 64) && text[64] == ' ';
}
static int check_inputs(void) {
    for (size_t i = 0; i < sizeof(pins) / sizeof(*pins); i++) {
        if (!pinned(pins[i].path, pins[i].sha256)) {
            fprintf(stderr, "ANDROID17_INPUT_REFUSED index=%zu\n", i);
            return 0;
        }
    }
    return 1;
}

static int enough_memory(void) {
    FILE *input = fopen("/proc/meminfo", "re");
    if (!input)
        return 0;
    char line[256];
    unsigned long long available = 0;
    while (fgets(line, sizeof(line), input))
        if (sscanf(line, "MemAvailable: %llu kB", &available) == 1)
            break;
    fclose(input);
    return available >= (PLB_ANDROID4G_MEMORY_MIB + 1536ULL) * 1024;
}
static int prepare_memory(int owner, int monitor, int prepare_for_folios) {
    /* MemAvailable can be mostly fragmented/reclaimable cache. The explicit
     * comparison mode prepares before backing exists even with enough bytes. */
    if (!prepare_for_folios && enough_memory())
        return 0;

    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(126);
        char descriptor[32];
        snprintf(descriptor, sizeof(descriptor), "%d", owner);
        if (fcntl(owner, F_SETFD, 0) || setenv("PLB_DIAGNOSTIC_OWNER_FD", descriptor, 1))
            _exit(127);
        execl(PLB_ANDROID4G_RUNTIME "/pressure-check", "pressure-check", "detached", "reclaim-6g",
              (char *)NULL);
        _exit(127);
    }
    if (child < 0)
        return -1;
    int status = 0;
    for (int tick = 0; tick < 320 && !stopping; tick++) {
        int done = waitpid(child, &status, WNOHANG);
        if (done == child)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
        if ((done < 0 && errno != EINTR) || !plb_host_memory_healthy() ||
            plb_diagnostic_kernel_bad_with_traces(monitor, 1))
            break;
        usleep(100000);
    }
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return -1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int check = argc == 2 && !strcmp(argv[1], "--check");
    int gate = argc == 2 && !strcmp(argv[1], "--folio-gate");
    int reclaim = argc == 2 && !strcmp(argv[1], "--run-approved-gles-reclaim");
    int run = (argc == 2 && !strcmp(argv[1], "--run-approved-gles")) || reclaim;
    if ((!check && !gate && !run) || !platform())
        return 40;
    int maintenance = profile_maintenance_lock(LOCK_SH), owner = profile_lock();
    if (maintenance < 0 || owner < 0)
        return 41;
    if (!check_inputs())
        return 42;
    if (android_isolate_owned_controller(PLB_VIEWER_UID))
        return 54;
    if (check) {
        puts("ANDROID17_INPUTS_CHECK_PASS");
        return 0;
    }
    int monitor = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (monitor < 0 || lseek(monitor, 0, SEEK_END) < 0)
        return 43;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGPIPE, SIG_IGN);
    if (!gate && (prepare_memory(owner, monitor, reclaim) || !enough_memory()))
        return 55;
    struct plb_shmem_guard policy = {.guardian = -1, .alive_write = -1};
    if (plb_shmem_begin(&policy, owner))
        return 44;
    int ready[2] = {-1, -1};
    if (!gate && pipe2(ready, O_CLOEXEC | O_NONBLOCK)) {
        plb_shmem_finish(&policy);
        return 45;
    }
    struct sensor_endpoint control = {.listener = -1, .client = -1};
    struct sensor_endpoint data = {.listener = -1, .client = -1};
    struct sensor_endpoint oemlock = {.listener = -1, .client = -1};
    struct sensor_endpoint console = {.listener = -1, .client = -1};
    if (!gate && (sensor_listen(&control, BASE "/sensor-control.sock") ||
                  sensor_listen(&data, BASE "/sensor-data.sock") ||
                  sensor_listen(&oemlock, BASE "/oemlock.sock") ||
                  sensor_listen(&console, BASE "/early-console.sock"))) {
        plb_shmem_finish(&policy);
        return 46;
    }
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(126);
        char text[32];
        snprintf(text, sizeof(text), "%d", owner);
        if (fcntl(owner, F_SETFD, 0) || setenv("PLB_DIAGNOSTIC_OWNER_FD", text, 1))
            _exit(127);
        if (gate) {
            execl(PLB_ANDROID4G_RUNTIME "/allocation-probe", "allocation-probe", "--small-folios",
                  (char *)NULL);
        } else {
            close(ready[0]);
            snprintf(text, sizeof(text), "%d", ready[1]);
            if (fcntl(ready[1], F_SETFD, 0) || setenv(PLB_PREALLOCATED_READY_ENV, text, 1))
                _exit(127);
            execl(PLB_ANDROID4G_PROXY, PLB_ANDROID4G_PROXY, (char *)NULL);
        }
        _exit(127);
    }
    if (child < 0) {
        plb_shmem_finish(&policy);
        return 47;
    }
    if (ready[1] >= 0)
        close(ready[1]);
    int status = 0, done = 0, restored = 0, result = 124;
    pthread_t console_thread = 0, observer = 0;
    struct gpu_console_context context = {&console, child};
    int console_started =
        !gate && !pthread_create(&console_thread, NULL, gpu_console_worker, &context);
    int observer_started = !gate && !pthread_create(&observer, NULL, android_memory_observer, NULL);
    long long deadline = plb_shmem_millis() + (gate ? 12000 : 180000);
    while (!stopping && plb_shmem_millis() < deadline) {
        if (!plb_host_memory_healthy() || plb_diagnostic_kernel_bad_with_traces(monitor, 1)) {
            result = 48;
            break;
        }
        done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            break;
        }
        if (done < 0 && errno != EINTR) {
            result = 49;
            break;
        }
        if (!gate && !restored) {
            char byte = 0;
            if (read(ready[0], &byte, 1) == 1) {
                if (byte != 'R' || plb_shmem_finish(&policy)) {
                    result = 50;
                    break;
                }
                restored = 1;
                puts("ANDROID17_RAM_PREPARED_POLICIES_RESTORED");
            }
            if (plb_shmem_millis() - policy.started_ms > 30000) {
                result = 51;
                break;
            }
        }
        if (!gate) {
            sensor_poll(&control, 0);
            sensor_poll(&data, 0);
            sensor_poll(&oemlock, 1);
            if (!plb_android4g_resource_limits(child)) {
                /* The child may exit between the initial wait and /proc read.
                 * Preserve its actual failure instead of calling a missing RSS
                 * record a memory-limit violation. Never signal a reaped PID. */
                done = waitpid(child, &status, WNOHANG);
                result = done == child
                             ? (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status))
                             : 52;
                break;
            }
        }
        usleep(100000);
    }
    if (plb_shmem_finish(&policy))
        result = 53;
    stopping = 1;
    if (done != child) {
        kill(child, SIGTERM);
        for (int i = 0; i < 80; i++) {
            done = waitpid(child, &status, WNOHANG);
            if (done == child)
                break;
            usleep(25000);
        }
        if (done != child) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
    if (console_started)
        pthread_join(console_thread, NULL);
    if (observer_started)
        pthread_join(observer, NULL);
    if (!gate) {
        sensor_close(&console, BASE "/early-console.sock");
        sensor_close(&control, BASE "/sensor-control.sock");
        sensor_close(&data, BASE "/sensor-data.sock");
        sensor_close(&oemlock, BASE "/oemlock.sock");
    }
    if (ready[0] >= 0)
        close(ready[0]);
    close(monitor);
    close(owner);
    close(maintenance);
    printf("ANDROID17_DEVELOPER_SESSION_EXIT %d\n", result);
    return result;
}
