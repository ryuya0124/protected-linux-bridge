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
#ifdef PLB_ANDROID17_APP_SESSION
static uid_t app_uid(void) {
    struct stat directory;
    if (lstat("/data/user/0/net.ryuya_dev.protectedlinux", &directory) ||
        !S_ISDIR(directory.st_mode) || directory.st_uid < 10000 || directory.st_uid > 19999)
        return (uid_t)-1;
    return directory.st_uid;
}
#endif
#include "pins.generated.h"
#include "android_owned_process.h"
#include "android_sensor_transport.h"
#include "android_sensor_pipe.h"
static volatile _Atomic sig_atomic_t stopping;
#include "android_gpu_console.h"
#define PLB_ANDROID_ADOPTED_PROBE 1
#ifdef PLB_ANDROID17_APP_SESSION
#include "android17_app_session.h"
#else
#include "android_memory_observer.h"
#endif

static void stop(int number) {
    (void)number;
    stopping = 1;
}

/* The app's private stdin capability remains private: never print its token.
 * Forward exactly the original 40 bytes to the VMM publisher on a distinct pipe.
 * This prevents Java buffering from consuming the controller's sensor stream. */
static int sensor_bootstrap(int *child_input, uint32_t *selected) {
    unsigned char header[PLB_SENSOR_PIPE_HEADER_BYTES];
    size_t used = 0;
    long long deadline = plb_shmem_millis() + 5000;
    while (used < sizeof(header) && plb_shmem_millis() < deadline) {
        struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
        if (poll(&input, 1, 100) <= 0)
            continue;
        ssize_t count = read(STDIN_FILENO, header + used, sizeof(header) - used);
        if (count <= 0)
            return -1;
        used += (size_t)count;
    }
    if (used != sizeof(header) || !plb_sensor_pipe_header(header, PLB_VIEWER_UID, selected))
        return -1;
    int channel[2];
    if (pipe2(channel, O_CLOEXEC))
        return -1;
    int okay = write(channel[1], header, 40) == 40;
    close(channel[1]);
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    if (!okay || flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK)) {
        close(channel[0]);
        return -1;
    }
    *child_input = channel[0];
    return 0;
}

static int sensor_app_poll(struct sensor_endpoint *control, struct sensor_endpoint *data,
                           uint32_t selected, unsigned long long *forwarded) {
    static unsigned char frame[PLB_SENSOR_PIPE_SAMPLE_BYTES];
    static size_t used;
    /* Bound each pass, even if the app violates the normal 50 Hz producer limit. */
    for (int i = 0; i < 16; i++) {
        ssize_t count = read(STDIN_FILENO, frame + used, sizeof(frame) - used);
        if (count < 0 && (errno == EAGAIN || errno == EINTR))
            return 0;
        if (count <= 0)
            return -1;
        used += (size_t)count;
        if (used != sizeof(frame))
            continue;
        struct plb_sensor_pipe_sample sample;
        if (!plb_sensor_pipe_decode(frame, selected, &sample))
            return -1;
        used = 0;
        int sent = sensor_forward_sample(control, data, sample.id, sample.timestamp_ns,
                                         sample.values, sample.count);
        if (sent < 0)
            return -1;
        *forwarded += sent;
    }
    return 0;
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

static int child_is_zombie(pid_t child) {
    char path[96], line[256];
    snprintf(path, sizeof(path), "/proc/%ld/status", (long)child);
    FILE *input = fopen(path, "re");
    if (!input)
        return 0;
    int zombie = 0;
    char state;
    while (fgets(line, sizeof(line), input)) {
        if (sscanf(line, "State: %c", &state) == 1) {
            zombie = state == 'Z';
            break;
        }
    }
    fclose(input);
    return zombie;
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
    int relay_sensors = argc == 2 && !strcmp(argv[1], "--run-approved-gles-sensors");
    reclaim |= relay_sensors;
    int run = (argc == 2 && !strcmp(argv[1], "--run-approved-gles")) || reclaim;
    if ((!check && !gate && !run) || !platform())
        return 40;
    int maintenance = profile_maintenance_lock(LOCK_SH), owner = profile_lock();
    if (maintenance < 0 || owner < 0)
#ifdef PLB_ANDROID17_APP_SESSION
        return 28;
#else
        return 41;
#endif
    if (!check_inputs())
#ifdef PLB_ANDROID17_APP_SESSION
        return 31;
#else
        return 42;
#endif
    if (android_isolate_owned_controller(PLB_VIEWER_UID))
        return 54;
#ifdef PLB_ANDROID17_APP_SESSION
    for (size_t i = 0; i < 3; i++) {
        static const char *names[] = {"android-work.qcow2", "android-misc.raw", "android-frp.raw"};
        char path[512];
        struct stat info;
        snprintf(path, sizeof(path), BASE "/%s", names[i]);
        if (lstat(path, &info) || !S_ISREG(info.st_mode) || info.st_uid || info.st_nlink != 1 ||
            (info.st_mode & 077) || info.st_size <= 0)
            return 21;
    }
#endif
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
#ifdef PLB_ANDROID17_APP_SESSION
    if (relay_sensors || gate || app_session_listen())
        return 58;
#endif
    int child_input = -1;
    uint32_t selected = 0;
    unsigned long long forwarded = 0;
    if (relay_sensors && sensor_bootstrap(&child_input, &selected))
        return 56;
#ifdef PLB_ANDROID17_APP_SESSION
    if (app_capability_pipe(&child_input))
        return 56;
#endif
    if (!gate && (prepare_memory(owner, monitor, reclaim) || !enough_memory()))
        return 55;
    struct plb_shmem_guard policy = {.guardian = -1, .alive_write = -1};
    if (plb_shmem_begin(&policy, owner))
        return 44;
#ifdef PLB_ANDROID17_APP_SESSION
    pid_t gate_parent = getpid(), gate_child = fork();
    if (!gate_child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != gate_parent)
            _exit(126);
        char descriptor[32];
        snprintf(descriptor, sizeof(descriptor), "%d", owner);
        if (fcntl(owner, F_SETFD, 0) || setenv("PLB_DIAGNOSTIC_OWNER_FD", descriptor, 1))
            _exit(127);
        execl(PLB_ANDROID4G_RUNTIME "/allocation-probe", "allocation-probe", "--small-folios",
              (char *)NULL);
        _exit(127);
    }
    int gate_status = 0, gate_done = 0;
    if (gate_child > 0) {
        for (int i = 0; i < 100 && !stopping; i++) {
            gate_done = waitpid(gate_child, &gate_status, WNOHANG);
            if (gate_done != 0)
                break;
            usleep(100000);
        }
        if (gate_done != gate_child) {
            kill(gate_child, SIGKILL);
            while (waitpid(gate_child, &gate_status, 0) < 0 && errno == EINTR) {
            }
        }
    }
    if (gate_child < 0 || gate_done != gate_child || !WIFEXITED(gate_status) ||
        WEXITSTATUS(gate_status) || !plb_host_memory_healthy() ||
        plb_diagnostic_kernel_bad_with_traces(monitor, 1)) {
        plb_shmem_finish(&policy);
        return 59;
    }
    puts("ANDROID17_APP_FOLIO_GATE_PASS");
#endif
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
    if (relay_sensors)
        sensor_enable_relay(&control, selected);
    pid_t parent = getpid(), child = fork();
    if (!child) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(126);
        char text[32];
        snprintf(text, sizeof(text), "%d", owner);
        if (fcntl(owner, F_SETFD, 0) || setenv("PLB_DIAGNOSTIC_OWNER_FD", text, 1))
            _exit(127);
        if (child_input >= 0) {
            if (dup2(child_input, STDIN_FILENO) < 0)
                _exit(127);
            close(child_input);
        }
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
    if (child_input >= 0)
        close(child_input);
    if (ready[1] >= 0)
        close(ready[1]);
    int status = 0, done = 0, restored = 0, result = 124;
    pthread_t console_thread = 0, observer = 0;
    struct gpu_console_context context = {&console, child};
    int console_started =
        !gate && !pthread_create(&console_thread, NULL, gpu_console_worker, &context);
#ifdef PLB_ANDROID17_APP_SESSION
    (void)observer;
    android_network_owner_fd = owner;
    android_touch_vmm = child;
    app_thread_started = !pthread_create(&app_thread, NULL, app_session_worker, NULL);
    if (!app_thread_started)
        stopping = 1;
#else
    int observer_started = !gate && !pthread_create(&observer, NULL, android_memory_observer, NULL);
#endif
#ifdef PLB_ANDROID17_APP_SESSION
    /* User-selected sessions live until disconnect, guest exit or a monitored failure. */
    long long deadline = 0;
#else
    long long deadline = plb_shmem_millis() + (gate ? 12000 : 180000);
#endif
    while (!stopping && (!deadline || plb_shmem_millis() < deadline)) {
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
            if (relay_sensors && sensor_app_poll(&control, &data, selected, &forwarded)) {
                result = 57;
                break;
            }
            if (!plb_android4g_resource_limits(child)) {
                /* The child may exit between the initial wait and /proc read.
                 * Preserve its actual failure instead of calling a missing RSS
                 * record a memory-limit violation. Never signal a reaped PID. */
                done = waitpid(child, &status, WNOHANG);
                /* Linux drops VmRSS before a dying child is waitable. Only an
                 * explicitly observed zombie gets this bounded reap window;
                 * live/unreadable tasks and actual memory-limit failures still
                 * fail closed. Never signal or reuse an already reaped PID. */
                if (done == 0 && child_is_zombie(child)) {
                    for (int i = 0; i < 10 && done == 0; i++) {
                        usleep(10000);
                        done = waitpid(child, &status, WNOHANG);
                    }
                }
                result = done == child
                             ? (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status))
                             : 52;
                break;
            }
        }
        usleep(relay_sensors ? 20000 : 100000);
    }
    if (plb_shmem_finish(&policy))
        result = 53;
    stopping = 1;
#ifdef PLB_ANDROID17_APP_SESSION
    app_session_shutdown(done != child);
#endif
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
#ifndef PLB_ANDROID17_APP_SESSION
    if (observer_started)
        pthread_join(observer, NULL);
#endif
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
    if (relay_sensors)
        printf("ANDROID17_SENSOR_RELAY mask=%u forwarded=%llu\n", selected, forwarded);
    printf("ANDROID17_DEVELOPER_SESSION_EXIT %d\n", result);
    return result;
}
