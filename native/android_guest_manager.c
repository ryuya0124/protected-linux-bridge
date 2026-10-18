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

#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#define BASE FILES "/android-guests/cuttlefish17"
#define KERNEL FILES "/kernels/android17-cuttlefish/Image"
#define PLB_OWNER_LOCK_FILE FILES "/vm-owner.lock"
#include "profile_lock.h"

#include "android_sensor_transport.h"

static volatile sig_atomic_t stopping;
static void stop(int signal) {
    (void)signal;
    stopping = 1;
}

#include "android_guest_display.h"

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
#ifdef PLB_ANDROID_PROBE
    (void)argv;
    if (getuid() || argc != 1)
        return 20;
#else
    check_only = argc == 2 && !strcmp(argv[1], "--check");
    if (getuid() || argc != 2 || (!check_only && strlen(argv[1]) != 5))
        return 20;
    char *end;
    unsigned long parsed = check_only ? 10000 : strtoul(argv[1], &end, 10);
    if ((!check_only && *end) || parsed < 10000 || parsed > 19999)
        return 20;
    app_uid = (uid_t)parsed;
#endif
    if (!private_file(KERNEL) || !private_file(BASE "/android-base.qcow2") ||
        !private_file(BASE "/android-work.qcow2") || !private_file(BASE "/android-lowram.cpio") ||
        !private_file(BASE "/android-misc.raw") || !private_file(BASE "/android-frp.raw"))
        return 21;
    if (!pinned_digest(KERNEL,
                       "f722bdd809bfd3485917a564b053b8a6d4938408734091f420c3297a8e3b20c8") ||
        !pinned_digest(BASE "/android-lowram.cpio",
                       "7e0c04dd3f3b9b92398c1e5ac0161e93d78c450e857d530d55012a6a550315e2"))
        return 31;
    if (check_only)
        return 0;
    FILE *info = fopen("/proc/meminfo", "re");
    if (!info)
        return 22;
    char line[128];
    unsigned long long available = 0;
    while (fgets(line, sizeof(line), info))
        if (sscanf(line, "MemAvailable: %llu kB", &available) == 1)
            break;
    fclose(info);
    if (available < 1280ULL * 1024)
        return 23;
    int lock = profile_lock();
    if (lock < 0)
        return 28;
    umask(077);
    if (clear_log(BASE "/android-console.log"))
        return 24;
    struct sensor_endpoint control, data, oemlock;
    if (sensor_listen(&control, BASE "/sensor-control.sock") ||
        sensor_listen(&data, BASE "/sensor-data.sock") ||
        sensor_listen(&oemlock, BASE "/oemlock.sock"))
        return 30;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    pid_t parent = getpid(), child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(25);
        unsetenv("LD_PRELOAD");
        unsetenv("LD_LIBRARY_PATH");
        const char *base_args[] = {
            "crosvm",
            "run",
            "--name",
            "plb-android17-probe",
            "--disable-sandbox",
            "--no-balloon",
            "--no-usb",
            "--protected-vm-without-firmware",
            "--swiotlb",
            "32",
            "--mem",
            "size=512",
            "--cpus",
            "num-cores=2",
            "--serial",
#ifdef PLB_ANDROID_PROBE
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
            "androidboot.hardware.egl=angle androidboot.hardware.vulkan=pastel "
            "androidboot.vendor.apex.com.android.hardware.gatekeeper=com.android.hardware."
            "gatekeeper.nonsecure "
            "androidboot.vendor.apex.com.android.hardware.keymint=com.android.hardware.keymint."
            "rust_nonsecure "
            "androidboot.vendor.apex.com.android.hardware.graphics.composer=com.android.hardware."
            "graphics.composer.ranchu "
            "androidboot.vendor.apex.com.google.emulated.camera.provider.hal=com.google.emulated."
            "camera.provider.hal "
            "androidboot.cpuvulkan.version=4202496 androidboot.opengles.version=196609 "
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
            "backend=2d,width=480,height=800",
            "--initrd",
            BASE "/android-lowram.cpio",
            KERNEL,
            NULL};
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
        args[n++] = KERNEL;
        args[n] = NULL;
        execv("/apex/com.android.virt/bin/crosvm", (char *const *)args);
        _exit(26);
    }
    if (child < 0)
        return 27;
#ifndef PLB_ANDROID_PROBE
    int display_started = android_display_start(app_uid) == 0;
    if (!display_started) {
        stopping = 1;
    }
#else
    (void)app_uid;
#endif
    int status = 0;
    pid_t done = 0;
#ifdef PLB_ANDROID_PROBE
    const int ticks = 3000;
#else
    const int ticks = 18000;
#endif
    for (int i = 0; i < ticks && !stopping; i++) {
        sensor_poll(&control, 0);
        sensor_poll(&data, 0);
        sensor_poll(&oemlock, 1);
        done = poll_child(child, &status);
        if (done)
            break;
        usleep(100000);
    }
#ifndef PLB_ANDROID_PROBE
    stopping = 1;
    if (display_started)
        android_display_stop();
#endif
    if (!done) {
#ifndef PLB_ANDROID_PROBE
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
        sensor_close(&control, BASE "/sensor-control.sock");
        sensor_close(&data, BASE "/sensor-data.sock");
        sensor_close(&oemlock, BASE "/oemlock.sock");
        return 124;
    }
    sensor_close(&control, BASE "/sensor-control.sock");
    sensor_close(&data, BASE "/sensor-data.sock");
    sensor_close(&oemlock, BASE "/oemlock.sock");
    close(lock);
    return done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 29;
}
