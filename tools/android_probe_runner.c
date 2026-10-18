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

#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#define BASE FILES "/android-guests/cuttlefish17"
#define KERNEL FILES "/kernels/android17-cuttlefish/Image"
#define PLB_OWNER_LOCK_FILE FILES "/vm-owner.lock"
#include "../native/profile_lock.h"

static volatile sig_atomic_t stopping;
static void stop(int signal) {
    (void)signal;
    stopping = 1;
}

static int private_file(const char *path) {
    struct stat file;
    return !lstat(path, &file) && S_ISREG(file.st_mode) && !file.st_uid && file.st_nlink == 1 &&
           !(file.st_mode & 077);
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
    (void)argv;
    if (getuid() || argc != 1)
        return 20;
    if (!private_file(KERNEL) || !private_file(BASE "/android-base.qcow2") ||
        !private_file(BASE "/android-work.qcow2") || !private_file(BASE "/android-ramdisk.cpio") ||
        !private_file(BASE "/android-misc.raw"))
        return 21;
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
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    pid_t parent = getpid(), child = fork();
    if (!child) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            _exit(25);
        unsetenv("LD_PRELOAD");
        unsetenv("LD_LIBRARY_PATH");
        execl("/apex/com.android.virt/bin/crosvm", "crosvm", "run", "--name", "plb-android17-probe",
              "--disable-sandbox", "--no-balloon", "--protected-vm-without-firmware", "--swiotlb",
              "32", "--mem", "size=512", "--cpus", "num-cores=1", "--serial",
              "type=file,path=" BASE "/android-console.log,console=true,earlycon=true", "--params",
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
              "--vsock", "cid=39821", "--block", "path=" BASE "/android-work.qcow2,ro=false",
              "--block", "path=" BASE "/android-misc.raw,ro=false", "--gpu",
              "backend=2d,width=480,height=800", "--initrd", BASE "/android-ramdisk.cpio", KERNEL,
              (char *)NULL);
        _exit(26);
    }
    if (child < 0)
        return 27;
    int status = 0;
    pid_t done = 0;
    for (int i = 0; i < 450 && !stopping; i++) {
        done = poll_child(child, &status);
        if (done)
            break;
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
    return done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 29;
}
