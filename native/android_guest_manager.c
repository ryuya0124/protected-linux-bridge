/* MIT. Fixed Android guest-only boot probe; no existing Debian disk attached. */
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
#include <stddef.h>
#include <stdatomic.h>

#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#if defined(PLB_ANDROID_GFXSTREAM_VULKAN_PROBE) && !defined(PLB_ANDROID_GFXSTREAM_PROBE)
#error "The Vulkan diagnostic requires the isolated GPU probe profile"
#endif
#if defined(PLB_ANDROID_GFXSTREAM_PROBE) && !defined(PLB_ANDROID_PROBE)
#error "Unverified GPU probe must never be enabled in the APK"
#endif
#if defined(PLB_ANDROID_MEMORY_PROBE) && !defined(PLB_ANDROID_PROBE)
#error "Android memory diagnostics must never be enabled in the APK"
#endif
#if defined(PLB_ANDROID_MEMORY_POLICY) && !defined(PLB_ANDROID_MEMORY_PROBE)
#error "The policy diagnostic requires the isolated Android memory profile"
#endif
#if defined(PLB_ANDROID_GPU_RENDER_PROBE) && !defined(PLB_ANDROID_GPU_MEMORY_PROBE) &&             \
    !defined(PLB_ANDROID_ADOPTED_PROBE)
#error "The guest render test is restricted to the guarded GPU memory diagnostic"
#endif
#if defined(PLB_ANDROID_GPU_MEMORY_PROBE) &&                                                       \
    (!defined(PLB_ANDROID_MEMORY_PROBE) || !defined(PLB_ANDROID_GFXSTREAM_PROBE) ||                \
     !defined(PLB_ANDROID_MEMORY_POLICY) || PLB_ANDROID_MEMORY_MIB != 1536 ||                      \
     defined(PLB_ANDROID_GFXSTREAM_VULKAN_PROBE))
#error "The combined research build is restricted to the pinned 1536 MiB GLES profile"
#endif
#if defined(PLB_ANDROID_MEMORY_PROBE) && defined(PLB_ANDROID_GFXSTREAM_PROBE) &&                   \
    !defined(PLB_ANDROID_GPU_MEMORY_PROBE)
#error "Keep Android memory and GPU diagnostics independent"
#endif
#if defined(PLB_ANDROID_LARGE_PROFILE) &&                                                          \
    (defined(PLB_ANDROID_PROBE) || defined(PLB_ANDROID_GFXSTREAM_PROBE))
#error "Keep the integrated large profile separate from diagnostics"
#endif
#if defined(PLB_ANDROID_ADOPTED_PROBE) &&                                                          \
    (!defined(PLB_ANDROID_PROBE) || !defined(PLB_ANDROID_GFXSTREAM_PROBE) ||                       \
     (!defined(PLB_ANDROID_GPU_RENDER_PROBE) && !defined(PLB_ANDROID4G_INTERACTIVE)) ||            \
     defined(PLB_ANDROID_MEMORY_PROBE) || defined(PLB_ANDROID_GPU_MEMORY_PROBE) ||                 \
     defined(PLB_ANDROID_LARGE_PROFILE) || defined(PLB_ANDROID_GFXSTREAM_VULKAN_PROBE) ||          \
     defined(PLB_GPU_WARNING_ONCE))
#error "Adopted Android RAM requires the isolated healthy-boot 4 GiB GLES diagnostic"
#endif
#if defined(PLB_ANDROID4G_CUSTOM_GUEST) && !defined(PLB_ANDROID_ADOPTED_PROBE)
#error "Custom guest kernel requires the isolated adopted-RAM Android diagnostic"
#endif
#if defined(PLB_ANDROID4G_LOADER_COMPAT) &&                                                        \
    (!defined(PLB_ANDROID_ADOPTED_PROBE) || !defined(PLB_ANDROID4G_BUFFER_CAPSET))
#error "Loader compatibility requires the isolated Android buffer-capset manager"
#endif
#if defined(PLB_ANDROID4G_INTERACTIVE) &&                                                          \
    (!defined(PLB_ANDROID_ADOPTED_PROBE) || !defined(PLB_ANDROID4G_CUSTOM_GUEST) ||                \
     defined(PLB_ANDROID_GPU_RENDER_PROBE))
#error "Interactive GPU requires the separate adopted custom guest without the render probe"
#endif
#ifdef PLB_ANDROID_ADOPTED_PROBE
#include "../tools/android4g_config.h"
#include "android4g.generated.h"
#include "../tools/preallocated_ready.h"
#include "../tools/diagnostic_kernel_monitor.h"
#include "../tools/android4g_resource_limits.h"
#define BASE PLB_ANDROID4G_BASE
#elif defined(PLB_ANDROID_LARGE_PROFILE)
#include "../tools/android_large_config.h"
#define BASE FILES "/android-guests/" PLB_LARGE_DIRECTORY
#define CHECK_BASE FILES "/android-guests/cuttlefish17"
#elif defined(PLB_ANDROID_MEMORY_PROBE)
#ifndef PLB_ANDROID_MEMORY_MIB
#define PLB_ANDROID_MEMORY_MIB 2048
#endif
#if PLB_ANDROID_MEMORY_MIB != 1536 && PLB_ANDROID_MEMORY_MIB != 2048
#error "Only prepared Android memory sizes are permitted"
#endif
#define PLB_STRINGIFY_INNER(value) #value
#define PLB_STRINGIFY(value) PLB_STRINGIFY_INNER(value)
#ifdef PLB_ANDROID_GPU_MEMORY_PROBE
#include "../tools/android_gpu_memory_config.h"
#define BASE FILES "/android-guests/" PLB_GPU_MEMORY_DIRECTORY
#else
#define BASE FILES "/android-guests/cf17-mem"
#endif
#elif defined(PLB_ANDROID_GFXSTREAM_PROBE)
#define BASE FILES "/android-guests/cuttlefish17-gpu-probe"
#else
#define BASE FILES "/android-guests/cuttlefish17"
#endif
#ifndef CHECK_BASE
#define CHECK_BASE BASE
#endif
#ifdef PLB_ANDROID_ADOPTED_PROBE
#define KERNEL PLB_ANDROID4G_KERNEL
#else
#define KERNEL FILES "/kernels/android17-cuttlefish/Image"
#endif
#define PLB_OWNER_LOCK_FILE FILES "/vm-owner.lock"
#if defined(PLB_ANDROID_MEMORY_PROBE) || defined(PLB_ANDROID_ADOPTED_PROBE)
#include "../tools/diagnostic_owner_lock.h"
#else
#include "profile_lock.h"
#endif

#include "android_sensor_transport.h"
#include "../tools/host_memory_health.h"
#ifdef PLB_GPU_WARNING_ONCE
#ifndef PLB_ANDROID_GPU_RENDER_PROBE
#error "The approved exception is restricted to the fixed guest render diagnostic"
#endif
#include "../tools/gpu_warning_once.h"
#endif

static volatile _Atomic sig_atomic_t stopping;
static void stop(int signal) {
    (void)signal;
    stopping = 1;
}

#include "android_guest_display.h"
#ifdef PLB_ANDROID4G_INTERACTIVE
#include "android_vmm_log.h"
#endif
#if (defined(PLB_ANDROID_MEMORY_PROBE) || defined(PLB_ANDROID_ADOPTED_PROBE)) &&                   \
    !defined(PLB_ANDROID4G_INTERACTIVE)
#include "../tools/android_memory_observer.h"
#endif

#ifdef PLB_ANDROID_GFXSTREAM_PROBE
#include <pthread.h>
#if !defined(PLB_ANDROID_MEMORY_PROBE) && !defined(PLB_ANDROID_ADOPTED_PROBE)
#include "android_guest_adb.h"
#include "android_guest_setup.generated.h"
#endif
#include "../tools/android_gpu_console.h"
#if !defined(PLB_ANDROID_GPU_MEMORY_PROBE) && !defined(PLB_ANDROID_ADOPTED_PROBE)
/* Configure guest swap as soon as its original adbd is ready, independently
 * of Mac ADB reconnection delays. Identity is checked before any guest write. */
static void *diagnostic_guest_setup(void *unused) {
    (void)unused;
    unsigned char *output = malloc(ANDROID_ADB_LIMIT + 1);
    if (!output)
        return NULL;
    const char identity[] = "exec:sh -c 'getprop ro.product.device; getprop ro.hardware; "
                            "getprop ro.build.version.incremental; id -u'";
    const char expected[] = "vsoc_arm64_only\ncutf_cvm\n16373615\n";
    for (int i = 0; i < 100 && !stopping; i++) {
        size_t size;
        if (!android_adb(identity, output, &size) && size < 256) {
            output[size] = 0;
            if (strncmp((char *)output, expected, strlen(expected)))
                break;
            const char *uid = (char *)output + strlen(expected);
            if (!strcmp(uid, "0\n")) {
                int result = android_adb(android_setup_service, output, &size);
                fprintf(stderr, "ANDROID_GPU_GUEST_SETUP result=%d ready=%d\n", result,
                        !result &&
                            memmem(output, size, "PLB_ANDROID_GUEST_SETUP_READY", 29) != NULL);
                break;
            }
            if (strcmp(uid, "2000\n"))
                break;
            android_adb("root:", output, &size);
        }
        usleep(100000);
    }
    free(output);
    return NULL;
}
#endif
#endif

#if defined(PLB_ANDROID_GPU_MEMORY_PROBE) || defined(PLB_ANDROID_ADOPTED_PROBE)
#include <sys/system_properties.h>
#include <sys/utsname.h>
static int gpu_memory_platform(void) {
    char value[PROP_VALUE_MAX];
    struct utsname kernel;
    __system_property_get("ro.product.model", value);
    if (strcmp(value, "SM-S948Q"))
        return 0;
    __system_property_get("ro.build.version.incremental", value);
    if (strcmp(value, "S948QOPS1AZHL") || uname(&kernel) ||
        strcmp(kernel.release, "6.12.30-android16-5-pd30ff70-abogkiS948QOPS1AZHL-4k"))
        return 0;
    __system_property_get("ro.build.version.sdk", value);
    if (strcmp(value, "36"))
        return 0;
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    char enforcing = 0;
    ssize_t size = fd >= 0 ? read(fd, &enforcing, 1) : -1;
    if (fd >= 0)
        close(fd);
    return size == 1 && enforcing == '1';
}
#endif

static int private_file(const char *path) {
    struct stat file;
    return !lstat(path, &file) && S_ISREG(file.st_mode) && !file.st_uid && file.st_nlink == 1 &&
           !(file.st_mode & 077);
}

static int pinned_digest(const char *path, const char *expected) {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC))
        return 0;
    pid_t pid = fork();
    if (!pid) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl("/system/bin/toybox", "toybox", "sha256sum", path, (char *)NULL);
        _exit(126);
    }
    close(pipefd[1]);
    char result[256];
    size_t used = 0;
    ssize_t count;
    while (used < sizeof(result) &&
           (count = read(pipefd[0], result + used, sizeof(result) - used)) > 0)
        used += (size_t)count;
    close(pipefd[0]);
    int status;
    if (pid < 0)
        return 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return 0;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && used > 65 &&
           !memcmp(result, expected, 64) && result[64] == ' ';
}

#ifdef PLB_ANDROID_LARGE_PROFILE
#include "android_large_profile.h"
#include "android_memory_prepare.h"
#endif

static int clear_log(const char *path) {
    struct stat info;
    if (lstat(path, &info))
        return errno == ENOENT ? 0 : -1;
    return private_file(path) ? unlink(path) : -1;
}

static pid_t poll_child(pid_t child, int *status) {
    pid_t result;
    do {
        result = waitpid(child, status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    return result;
}

int main(int argc, char **argv) {
    uid_t app_uid = 0;
    int check_only = 0;
#if defined(PLB_ANDROID_PROBE) && !defined(PLB_ANDROID4G_INTERACTIVE)
    check_only = argc == 2 && !strcmp(argv[1], "--check");
    if (getuid() || (!check_only && argc != 1))
        return 20;
#else
    check_only = argc == 2 && !strcmp(argv[1], "--check");
    if (getuid() ||
#ifdef PLB_ANDROID_LARGE_PROFILE
        (!check_only && (argc != 3 || strcmp(argv[2], "--ack-small-shmem"))) ||
#else
        argc != 2 ||
#endif
        (!check_only && strlen(argv[1]) != 5))
        return 20;
    char *end;
    unsigned long parsed = check_only ? 10000 : strtoul(argv[1], &end, 10);
    if ((!check_only && *end) || parsed < 10000 || parsed > 19999)
        return 20;
    app_uid = (uid_t)parsed;
#endif
#if defined(PLB_ANDROID_PROBE) || defined(PLB_ANDROID_LARGE_PROFILE)
    /* Refuse even a preflight before inspecting/preparing a new disk profile. */
    if (
#ifdef PLB_GPU_WARNING_ONCE
        !plb_gpu_once_admitted()
#else
        !plb_host_memory_healthy()
#endif
    ) {
        fprintf(stderr, "ANDROID_PROFILE_REJECTED host_memory_warning=1\n");
        return 42;
    }
#endif
#if defined(PLB_ANDROID_GPU_MEMORY_PROBE) || defined(PLB_ANDROID_ADOPTED_PROBE)
    if (!gpu_memory_platform())
        return 39;
#endif
    if (!private_file(KERNEL) ||
        !private_file(FILES "/android-guests/cuttlefish17/android-base.qcow2") ||
        !private_file(CHECK_BASE "/android-work.qcow2") ||
        !private_file(CHECK_BASE "/android-lowram.cpio") ||
        !private_file(CHECK_BASE "/android-misc.raw") ||
        !private_file(CHECK_BASE "/android-frp.raw"))
        return 21;
    if (!pinned_digest(KERNEL,
#ifdef PLB_ANDROID_ADOPTED_PROBE
                       PLB_ANDROID4G_KERNEL_SHA256
#else
                       "f722bdd809bfd3485917a564b053b8a6d4938408734091f420c3297a8e3b20c8"
#endif
                       ) ||
        !pinned_digest(CHECK_BASE "/android-lowram.cpio",
#ifdef PLB_ANDROID_ADOPTED_PROBE
                       PLB_ANDROID4G_INITRAMFS_SHA256
#elif defined(PLB_ANDROID_GFXSTREAM_PROBE)
                       "ebf7141a7d287fdd877065109b5e68dfa9339a5a460c8d5795ff7355a36dd143"
#else
                       "7e0c04dd3f3b9b92398c1e5ac0161e93d78c450e857d530d55012a6a550315e2"
#endif
                       ))
        return 31;
#if defined(PLB_ANDROID_GFXSTREAM_PROBE) && !defined(PLB_ANDROID_ADOPTED_PROBE)
    const char *vmm_digest = "9b4d56db70be1d3d646a08da0cb3e6eb95637457e5f15cb3b1c627f348e3e847";
    if (!pinned_digest("/apex/com.android.virt/bin/crosvm", vmm_digest))
        return 31;
#ifdef PLB_ANDROID_GPU_MEMORY_PROBE
    /* --check is also used by the policy supervisor before its first write. */
    if (!private_file(PLB_GPU_MEMORY_LIBRARY) ||
        !pinned_digest(PLB_GPU_MEMORY_LIBRARY, PLB_ANDROID_MEMORY_LIBRARY_SHA256) ||
        !private_file(FILES "/kernel-probes/crosvm-copy") ||
        !pinned_digest(FILES "/kernel-probes/crosvm-copy", vmm_digest))
        return 31;
#endif
#ifdef PLB_ANDROID_GFXSTREAM_VULKAN_PROBE
#ifndef PLB_GFXSTREAM_COMPAT_SHA256
#error "The Vulkan diagnostic compatibility library must be pinned by the build tool"
#endif
    if (!private_file(FILES "/kernel-probes/crosvm-copy") ||
        !pinned_digest(FILES "/kernel-probes/crosvm-copy", vmm_digest) ||
        !private_file(FILES "/kernel-probes/gfxstream-libs/libgfxstream_backend.so") ||
        !pinned_digest(FILES "/kernel-probes/gfxstream-libs/libgfxstream_backend.so",
                       "41414d640a526f56ce9a151f786e965a6e1fe3769e201790e0e58e7a31ff87da") ||
        !private_file(FILES "/kernel-probes/libgfxstream_android_compat.so") ||
        !pinned_digest(FILES "/kernel-probes/libgfxstream_android_compat.so",
                       PLB_GFXSTREAM_COMPAT_SHA256))
        return 31;
#endif
#endif
#ifdef PLB_ANDROID_ADOPTED_PROBE
#ifdef PLB_ANDROID4G_LOADER_COMPAT
#ifndef PLB_ANDROID4G_COMPAT_SHA256
#error "Pin the isolated Android loader compatibility library"
#endif
    if (!private_file(PLB_ANDROID4G_COMPAT) ||
        !pinned_digest(PLB_ANDROID4G_COMPAT, PLB_ANDROID4G_COMPAT_SHA256))
        return 31;
#endif
    if (!private_file(PLB_ANDROID4G_VMM) ||
        !pinned_digest(PLB_ANDROID4G_VMM, PLB_ANDROID4G_VMM_SHA256) ||
        !private_file(PLB_ANDROID4G_PROXY) ||
        !pinned_digest(PLB_ANDROID4G_PROXY, PLB_ANDROID4G_PROXY_SHA256) ||
        !pinned_digest("/apex/com.android.virt/lib64/libgfxstream_backend.so",
                       "41414d640a526f56ce9a151f786e965a6e1fe3769e201790e0e58e7a31ff87da"))
        return 31;
#endif
#ifdef PLB_ANDROID_LARGE_PROFILE
    if (!large_platform())
        return 39;
    if (!plb_shmem_original())
        return 38;
#endif
    if (check_only) {
        fprintf(stdout, "ANDROID_PROFILE_CHECK_OK memory_mib=%d\n",
#ifdef PLB_ANDROID_LARGE_PROFILE
                PLB_ANDROID_LARGE_MEMORY_MIB
#elif defined(PLB_ANDROID_ADOPTED_PROBE)
                PLB_ANDROID4G_MEMORY_MIB
#elif defined(PLB_ANDROID_MEMORY_PROBE)
                PLB_ANDROID_MEMORY_MIB
#else
                512
#endif
        );
        return 0;
    }
    FILE *info = fopen("/proc/meminfo", "re");
    if (!info)
        return 22;
    char line[128];
    unsigned long long available = 0;
    while (fgets(line, sizeof(line), info))
        if (sscanf(line, "MemAvailable: %llu kB", &available) == 1)
            break;
    fclose(info);
    if (available <
#ifdef PLB_ANDROID_LARGE_PROFILE
        PLB_LARGE_HEADROOM_KIB
#elif defined(PLB_ANDROID_ADOPTED_PROBE)
        (PLB_ANDROID4G_MEMORY_MIB + 1536ULL) * 1024
#elif defined(PLB_ANDROID_MEMORY_PROBE)
        (PLB_ANDROID_MEMORY_MIB + 768ULL) * 1024
#else
        1280ULL * 1024
#endif
    )
        return 23;
    int lock =
#if defined(PLB_ANDROID_MEMORY_PROBE) || defined(PLB_ANDROID_ADOPTED_PROBE)
        diagnostic_owner_lock();
#else
        profile_lock();
#endif
    if (lock < 0)
        return 28;
#ifdef PLB_ANDROID_ADOPTED_PROBE
    int kernel_monitor = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (kernel_monitor < 0 || lseek(kernel_monitor, 0, SEEK_END) < 0)
        return 43;
#endif
    umask(077);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    signal(SIGCHLD, SIG_DFL);
#ifdef PLB_ANDROID_LARGE_PROFILE
    if (android_isolate_owned_controller(app_uid))
        return 41;
    if (large_profile_files(app_uid) || !private_file(BASE "/android-work.qcow2") ||
        !private_file(BASE "/android-misc.raw") || !private_file(BASE "/android-frp.raw") ||
        !pinned_digest(BASE "/android-lowram.cpio",
                       "7e0c04dd3f3b9b92398c1e5ac0161e93d78c450e857d530d55012a6a550315e2"))
        return 35;
#endif
    if (clear_log(BASE "/android-console.log")
#ifdef PLB_ANDROID4G_INTERACTIVE
        || clear_log(BASE "/android-vmm.log")
#endif
#if defined(PLB_ANDROID_MEMORY_PROBE) || defined(PLB_ANDROID_ADOPTED_PROBE)
        || clear_log(BASE "/android-memory-frame.png")
#endif
#ifdef PLB_ANDROID_GPU_RENDER_PROBE
        || clear_log(BASE "/android-gpu-probe-result.log") ||
        clear_log(BASE "/android-gpu-probe-frame.png")
#endif
    )
        return 24;
    struct sensor_endpoint control, data, oemlock;
    if (sensor_listen(&control, BASE "/sensor-control.sock") ||
        sensor_listen(&data, BASE "/sensor-data.sock") ||
        sensor_listen(&oemlock, BASE "/oemlock.sock"))
        return 30;
#ifdef PLB_ANDROID_GFXSTREAM_PROBE
    struct sensor_endpoint console;
    if (sensor_listen(&console, BASE "/early-console.sock"))
        return 30;
#endif
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
#ifdef PLB_ANDROID_LARGE_PROFILE
    struct plb_shmem_guard policy = {.guardian = -1, .alive_write = -1};
    int large_error = 0;
    if (android_display_listen(app_uid) || android_display_wait_for_client()) {
        large_error = 30;
        goto large_startup_failed;
    }
    if (android_prepare_memory() || stopping) {
        large_error = 40;
        goto large_startup_failed;
    }
    if (android_prepare_available() < PLB_LARGE_HEADROOM_KIB) {
        large_error = 23;
        goto large_startup_failed;
    }
    if (plb_shmem_begin(&policy, lock)) {
        large_error = 38;
        goto large_startup_failed;
    }
#endif
#ifdef PLB_ANDROID4G_INTERACTIVE
    int vmm_output[2];
    if (pipe2(vmm_output, O_CLOEXEC))
        return 24;
    // Status messages must not freeze the monitor when the app is cached.
    int output_flags = fcntl(STDERR_FILENO, F_GETFL);
    if (output_flags < 0 || fcntl(STDERR_FILENO, F_SETFL, output_flags | O_NONBLOCK))
        return 24;
    signal(SIGPIPE, SIG_IGN);
#endif
    pid_t parent = getpid(), child = fork();
    if (!child) {
#ifdef PLB_ANDROID4G_INTERACTIVE
        close(vmm_output[0]);
        if (dup2(vmm_output[1], STDOUT_FILENO) < 0 || dup2(vmm_output[1], STDERR_FILENO) < 0)
            _exit(24);
        close(vmm_output[1]);
#endif
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(25);
        unsetenv("LD_PRELOAD");
        unsetenv("LD_LIBRARY_PATH");
#ifdef PLB_ANDROID_ADOPTED_PROBE
        char owner_descriptor[16];
        snprintf(owner_descriptor, sizeof(owner_descriptor), "%d", lock);
        if (fcntl(lock, F_SETFD, 0) || setenv(PLB_DIAGNOSTIC_OWNER_ENV, owner_descriptor, 1))
            _exit(26);
        execl(PLB_ANDROID4G_PROXY, PLB_ANDROID4G_PROXY, (char *)NULL);
        _exit(26);
#else
#ifdef PLB_ANDROID_GFXSTREAM_VULKAN_PROBE
        /* Development-only Vulkan-loader compatibility. The ordinary GLES
         * diagnostic uses the installed VMM with no preload or private copy. */
        if (setenv("ANDROID_EMU_VK_LOADER_PATH", "/system/lib64/libvulkan.so", 1) ||
            setenv("GFXSTREAM_LOG_VERBOSE", "1", 1) ||
            setenv("LD_PRELOAD", FILES "/kernel-probes/libgfxstream_android_compat.so", 1) ||
            setenv("LD_LIBRARY_PATH",
                   FILES "/kernel-probes/gfxstream-libs:/apex/com.android.virt/lib64:/system/"
                         "lib64:/system_ext/lib64",
                   1))
            _exit(32);
#endif
#ifdef PLB_ANDROID_MEMORY_PROBE
        /* Ordinary per-process OOM preference: the diagnostic is the victim. */
        int oom = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
        if (oom < 0 || write(oom, "1000", 4) != 4)
            _exit(33);
        close(oom);
#ifdef PLB_ANDROID_MEMORY_POLICY
#ifndef PLB_ANDROID_MEMORY_LIBRARY_SHA256
#error "Pin the diagnostic shared-memory library"
#endif
        const char *memory_library =
#ifdef PLB_ANDROID_GPU_MEMORY_PROBE
            PLB_GPU_MEMORY_LIBRARY;
#else
            FILES "/kernel-probes/libandroid_memory_backing_probe.so";
#endif
        if (!private_file(memory_library) ||
            !pinned_digest(memory_library, PLB_ANDROID_MEMORY_LIBRARY_SHA256) ||
            !pinned_digest(FILES "/kernel-probes/crosvm-copy",
                           "9b4d56db70be1d3d646a08da0cb3e6eb95637457e5f15cb3b1c627f348e3e847") ||
            setenv("LD_PRELOAD", memory_library, 1) ||
            setenv("PLB_ANDROID_BACKING_PROBE",
#ifdef PLB_ANDROID_GPU_MEMORY_PROBE
                   "shared-middle-prefault",
#else
                   "shared-middle-refault",
#endif
                   1) ||
            setenv("LD_LIBRARY_PATH",
                   "/apex/com.android.virt/lib64:/system/lib64:/system_ext/lib64", 1))
            _exit(34);
#endif
#endif
#ifdef PLB_ANDROID_LARGE_PROFILE
        int oom = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
        if (oom < 0 || write(oom, "1000", 4) != 4)
            _exit(33);
        close(oom);
        if (setenv("LD_PRELOAD", PLB_LARGE_LIBRARY, 1) ||
            setenv("PLB_ANDROID_LARGE_BACKING", "shared-middle-prefault", 1) ||
            setenv("LD_LIBRARY_PATH",
                   "/apex/com.android.virt/lib64:/system/lib64:/system_ext/lib64", 1))
            _exit(34);
#endif
        const char *base_args[] = {
            "crosvm",
            "run",
            "--name",
#ifdef PLB_ANDROID_LARGE_PROFILE
            "plb-android17-large",
#else
            "plb-android17-probe",
#endif
            "--disable-sandbox",
            "--no-balloon",
            "--no-usb",
            "--protected-vm-without-firmware",
            "--swiotlb",
            "32",
            "--mem",
#ifdef PLB_ANDROID_LARGE_PROFILE
            PLB_LARGE_MEMORY_ARGUMENT,
#elif defined(PLB_ANDROID_MEMORY_PROBE)
            "size=" PLB_STRINGIFY(PLB_ANDROID_MEMORY_MIB),
#else
            "size=512",
#endif
            "--cpus",
            "num-cores=2",
            "--serial",
#ifdef PLB_ANDROID_GFXSTREAM_PROBE
            "type=unix-stream,path=" BASE "/early-console.sock,input-unix-stream=true,"
            "console=true,earlycon=true",
#elif defined(PLB_ANDROID_PROBE)
            "type=file,path=" BASE "/android-console.log,console=true,earlycon=true",
#else
            "type=sink,console=true,earlycon=true",
#endif
            "--params",
            "console=ttyS0 earlycon 8250.nr_uarts=1 rdinit=/init panic=0 printk.devkmsg=on "
            "androidboot.hardware=cutf_cvm androidboot.fstab_suffix=cf.f2fs.cts "
            "androidboot.force_normal_boot=1 androidboot.slot_suffix=_a "
            "androidboot.vbmeta.size=22912 androidboot.vbmeta.hash_alg=sha256 "
            "androidboot.vbmeta.digest="
            "0db3571720daf0bda012722eed14cfe195f4d986e8521c3c0ad2c49ee7351633 "
            "androidboot.boot_devices=10000.pci androidboot.hardware.gralloc=minigbm "
            "androidboot.hardware.hwcomposer=ranchu androidboot.hardware.hwcomposer.mode=client "
            "androidboot.hardware.hwcomposer.display_finder_mode=drm "
#ifdef PLB_ANDROID_GFXSTREAM_VULKAN_PROBE
            "androidboot.hardware.egl=emulation androidboot.hardware.vulkan=ranchu "
            "androidboot.hardware.gltransport=virtio-gpu-pipe androidboot.enable_bootanimation=0 "
            "androidboot.console=ttyS0 "
#elif defined(PLB_ANDROID_GFXSTREAM_PROBE)
            "androidboot.hardware.egl=emulation androidboot.hardware.vulkan=pastel "
            "androidboot.hardware.gltransport=virtio-gpu-pipe androidboot.enable_bootanimation=0 "
            "androidboot.hardware.guest_hwui_renderer=skiagl "
            "androidboot.hardware.guest_disable_renderer_preload=true "
            "androidboot.console=ttyS0 "
#else
            "androidboot.hardware.egl=angle androidboot.hardware.vulkan=pastel "
#endif
            "androidboot.vendor.apex.com.android.hardware.gatekeeper=com.android.hardware."
            "gatekeeper.nonsecure "
            "androidboot.vendor.apex.com.android.hardware.keymint=com.android.hardware.keymint."
            "rust_nonsecure "
            "androidboot.vendor.apex.com.android.hardware.graphics.composer=com.android.hardware."
            "graphics.composer.ranchu "
            "androidboot.vendor.apex.com.google.emulated.camera.provider.hal=com.google.emulated."
            "camera.provider.hal "
#ifdef PLB_ANDROID_GFXSTREAM_PROBE
            "androidboot.cpuvulkan.version=4202496 androidboot.opengles.version=196608 "
#else
            "androidboot.cpuvulkan.version=4202496 androidboot.opengles.version=196609 "
#endif
            "androidboot.config.low_ram=true androidboot.vsock_lights_port=6900",
            "--vsock",
            "cid=39821",
            "--block",
            "path=" BASE "/android-work.qcow2,ro=false",
            "--block",
            "path=" BASE "/android-misc.raw,ro=false",
            "--block",
            "path=" BASE "/android-frp.raw,ro=false",
            "--gpu",
#ifdef PLB_ANDROID_GFXSTREAM_VULKAN_PROBE
            "backend=gfxstream,context-types=gfxstream-gles:gfxstream-vulkan:gfxstream-composer,"
            "egl=true,gles=true,vulkan=true,glx=false,surfaceless=true,width=480,height=800",
#elif defined(PLB_ANDROID_GFXSTREAM_PROBE)
#ifdef PLB_ANDROID_GPU_MEMORY_PROBE
            PLB_GPU_MEMORY_GPU_ARGUMENT,
#else
            "backend=gfxstream,context-types=gfxstream-gles:gfxstream-composer,"
            "egl=true,gles=true,vulkan=false,glx=false,surfaceless=true,width=480,height=800",
#endif
#else
            "backend=2d,width=480,height=800",
#endif
            "--initrd",
            BASE "/android-lowram.cpio",
            KERNEL,
            NULL};
#ifdef PLB_ANDROID_MEMORY_POLICY
        /* Keep the original shared memfd/FD contract used by GPU and vsock. */
#endif
        char serial[4][256];
        const char *args[160];
        size_t n = 0;
        while (base_args[n]) {
            args[n] = base_args[n];
            n++;
        }
        /* Keep the kernel as the final positional argument. */
        n--;
        for (int port = 0; port < 4; port++) {
            if (port == 2)
                snprintf(serial[port], sizeof(serial[port]),
                         "hardware=virtio-console,num=3,type=sink");
            else
                snprintf(serial[port], sizeof(serial[port]),
                         "hardware=virtio-console,num=%d,type=unix-stream,path=%s,input-unix-"
                         "stream=true",
                         port + 1,
                         port == 0   ? BASE "/sensor-control.sock"
                         : port == 1 ? BASE "/sensor-data.sock"
                                     : BASE "/oemlock.sock");
            args[n++] = "--serial";
            args[n++] = serial[port];
        }
#if defined(PLB_ANDROID_MEMORY_POLICY) || defined(PLB_ANDROID_LARGE_PROFILE)
        args[n++] = "--hugepages";
#endif
        args[n++] = KERNEL;
        args[n] = NULL;
#ifdef PLB_ANDROID_LARGE_PROFILE
        execv(PLB_LARGE_VMM, (char *const *)args);
#elif defined(PLB_ANDROID_GFXSTREAM_VULKAN_PROBE) || defined(PLB_ANDROID_MEMORY_POLICY)
        execv(FILES "/kernel-probes/crosvm-copy", (char *const *)args);
#else
        execv("/apex/com.android.virt/bin/crosvm", (char *const *)args);
#endif
#endif /* PLB_ANDROID_ADOPTED_PROBE */
        _exit(26);
    }
    if (child < 0) {
#ifdef PLB_ANDROID_LARGE_PROFILE
        large_error = 27;
        goto large_startup_failed;
#else
        return 27;
#endif
    }
#ifdef PLB_ANDROID_ADOPTED_PROBE
    const char *ready = getenv(PLB_PREALLOCATED_READY_ENV);
    if (ready) {
        int descriptor = plb_ready_descriptor(ready, 0);
        unsetenv(PLB_PREALLOCATED_READY_ENV);
        if (descriptor >= 0)
            close(descriptor);
    }
#endif
#ifdef PLB_ANDROID_GFXSTREAM_PROBE
    pthread_t console_thread = 0;
    struct gpu_console_context console_context = {&console, child};
    int console_started =
        !pthread_create(&console_thread, NULL, gpu_console_worker, &console_context);
#if !defined(PLB_ANDROID_GPU_MEMORY_PROBE) && !defined(PLB_ANDROID_ADOPTED_PROBE)
    pthread_t bootstrap;
    int bootstrap_started = !pthread_create(&bootstrap, NULL, diagnostic_guest_setup, NULL);
#endif
#endif
#ifdef PLB_ANDROID4G_INTERACTIVE
    close(vmm_output[1]);
    struct android_vmm_log vmm_log;
    int vmm_log_started = !android_vmm_log_start(&vmm_log, vmm_output[0], BASE "/android-vmm.log");
    int interactive_failure = vmm_log_started && console_started ? 0 : 24;
    if (!console_started)
        stopping = 1;
    if (!vmm_log_started) {
        close(vmm_output[0]);
        stopping = 1;
    }
#endif
#if !defined(PLB_ANDROID_PROBE) || defined(PLB_ANDROID4G_INTERACTIVE)
#ifdef PLB_ANDROID4G_INTERACTIVE
    android_network_owner_fd = lock;
#endif
    int display_started = android_display_start(app_uid) == 0;
    if (!display_started) {
        stopping = 1;
    }
#else
    (void)app_uid;
#endif
#if (defined(PLB_ANDROID_MEMORY_PROBE) || defined(PLB_ANDROID_ADOPTED_PROBE)) &&                   \
    !defined(PLB_ANDROID4G_INTERACTIVE)
    pthread_t observer;
    int observer_started = !pthread_create(&observer, NULL, android_memory_observer, NULL);
#endif
    int status = 0;
    pid_t done = 0;
#ifdef PLB_ANDROID4G_SURFACE_INTERACTIVE
    const int ticks = 1800; /* Separate developer Surface session: at most three minutes. */
#elif defined(PLB_ANDROID4G_INTERACTIVE)
    const int ticks = 18000;
#elif defined(PLB_ANDROID_ADOPTED_PROBE)
    const int ticks = 1600;
#elif defined(PLB_ANDROID_MEMORY_PROBE)
    const int ticks = 1200;
#elif defined(PLB_ANDROID_PROBE)
    const int ticks = 3000;
#else
    const int ticks = 18000;
#endif
    for (int i = 0; i < ticks && !stopping; i++) {
#ifdef PLB_ANDROID4G_INTERACTIVE
        if (vmm_log_started && atomic_load(&vmm_log.fault)) {
            fprintf(stderr, "PLB_GUNYAH_PAGE_FAULT guest_error=1\n");
            interactive_failure = 33;
            stopping = 1;
            kill(child, SIGTERM);
            break;
        }
#endif
#ifdef PLB_ANDROID_ADOPTED_PROBE
        if (!plb_host_memory_healthy() || plb_diagnostic_kernel_bad(kernel_monitor)) {
#ifdef PLB_ANDROID4G_INTERACTIVE
            interactive_failure = 42;
#endif
            fprintf(stderr, "PLB_ANDROID_ADOPTED_HOST_FAILURE\n");
            stopping = 1;
            kill(child, SIGTERM); /* Owned unreaped child only. */
            break;
        }
#endif

#ifdef PLB_ANDROID_LARGE_PROFILE
        if (policy.guardian > 0 && plb_shmem_millis() - policy.started_ms >= 10000 &&
            plb_shmem_finish(&policy)) {
            large_error = 38;
            stopping = 1;
        }
#endif
        sensor_poll(&control, 0);
        sensor_poll(&data, 0);
        sensor_poll(&oemlock, 1);
        done = poll_child(child, &status);
        if (done)
            break;
#ifdef PLB_ANDROID_ADOPTED_PROBE
        if (!(i % 10) && !plb_android4g_resource_limits(child)) {
            /* The PID remains an owned, unreaped child even if it just exited. */
            done = poll_child(child, &status);
            if (!done) {
#ifdef PLB_ANDROID4G_INTERACTIVE
                interactive_failure = 23;
#endif
                fprintf(stderr, "PLB_ANDROID_ADOPTED_RESOURCE_LIMIT\n");
                stopping = 1;
                kill(child, SIGTERM);
            }
            break;
        }
#endif
        usleep(100000);
    }
#ifdef PLB_ANDROID_LARGE_PROFILE
    if (plb_shmem_finish(&policy))
        large_error = 38;
#endif
#if (defined(PLB_ANDROID_MEMORY_PROBE) || defined(PLB_ANDROID_ADOPTED_PROBE)) &&                   \
    !defined(PLB_ANDROID4G_INTERACTIVE)
    stopping = 1;
    if (observer_started)
        pthread_join(observer, NULL);
#endif
#if !defined(PLB_ANDROID_PROBE) || defined(PLB_ANDROID4G_INTERACTIVE)
    stopping = 1;
    if (display_started)
        android_display_stop();
#ifdef PLB_ANDROID4G_INTERACTIVE
    android_network_stop();
#endif
#endif
#ifdef PLB_ANDROID_GFXSTREAM_PROBE
    stopping = 1;
#if !defined(PLB_ANDROID_GPU_MEMORY_PROBE) && !defined(PLB_ANDROID_ADOPTED_PROBE)
    if (bootstrap_started)
        pthread_join(bootstrap, NULL);
#endif
#ifndef PLB_ANDROID4G_INTERACTIVE
    if (console_started)
        pthread_join(console_thread, NULL);
    sensor_close(&console, BASE "/early-console.sock");
    if (gpu_console_fd >= 0)
        close(gpu_console_fd);
#endif
#endif
    if (!done) {
#if !defined(PLB_ANDROID_PROBE) || defined(PLB_ANDROID4G_INTERACTIVE)
        android_guest_shutdown();
        for (int i = 0; i < 120 && !done; i++) {
            sensor_poll(&control, 0);
            sensor_poll(&data, 0);
            sensor_poll(&oemlock, 1);
            done = poll_child(child, &status);
            if (!done)
                usleep(100000);
        }
#endif
        if (!done)
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
#ifdef PLB_ANDROID4G_INTERACTIVE
        gpu_console_finish_worker(console_thread, console_started, &console);
        if (vmm_log_started)
            android_vmm_log_stop(&vmm_log);
        if (interactive_failure)
            return interactive_failure;
#endif
        sensor_close(&control, BASE "/sensor-control.sock");
        sensor_close(&data, BASE "/sensor-data.sock");
        sensor_close(&oemlock, BASE "/oemlock.sock");
#ifdef PLB_ANDROID_LARGE_PROFILE
        if (large_error)
            return large_error;
#endif
        return 124;
    }
#ifdef PLB_ANDROID4G_INTERACTIVE
    gpu_console_finish_worker(console_thread, console_started, &console);
    if (vmm_log_started)
        android_vmm_log_stop(&vmm_log);
    if (interactive_failure)
        return interactive_failure;
#endif
    sensor_close(&control, BASE "/sensor-control.sock");
    sensor_close(&data, BASE "/sensor-data.sock");
    sensor_close(&oemlock, BASE "/oemlock.sock");
    close(lock);
#ifdef PLB_ANDROID_GFXSTREAM_PROBE
    fprintf(stderr, "ANDROID_GPU_VMM_STATUS raw=%d signal=%d\n", status,
            WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
#ifdef PLB_ANDROID_LARGE_PROFILE
    if (large_error)
        return large_error;
#endif
    return done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 29;
#ifdef PLB_ANDROID_LARGE_PROFILE
large_startup_failed:
    plb_shmem_finish(&policy);
    android_display_stop();
    sensor_close(&control, BASE "/sensor-control.sock");
    sensor_close(&data, BASE "/sensor-data.sock");
    sensor_close(&oemlock, BASE "/oemlock.sock");
    close(lock);
    return large_error;
#endif
}
