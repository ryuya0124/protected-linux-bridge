/* MIT. Explicit 4 GiB Android/GPU app profile. Fixed artifacts and paths only.
 * Root installs verified copies into an independent runtime/profile. It never
 * changes a host kernel, partition, SELinux policy or another process's cgroup.
 * The policy child owns the bounded six-key restoration and the VM lifetime. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#define RUNTIME FILES "/android-gpu4g-runtime"
#define PROFILE FILES "/android-guests/cf17-gpu4096"
#define ORIGINAL FILES "/android-guests/cuttlefish17"
#include "profile_lock.h"
#include "../tools/host_memory_health.h"
#include "android_gpu4g_files.h"
struct gpu4g_artifact {
    const char *asset, *installed, *sha256;
};
#include "gpu4g_install.generated.h"

static int platform(void) {
    char value[PROP_VALUE_MAX];
    struct utsname kernel;
    __system_property_get("ro.product.model", value);
    if (strcmp(value, "SM-S948Q"))
        return 0;
    __system_property_get("ro.build.version.incremental", value);
    if (strcmp(value, "S948QOPS1AZHL"))
        return 0;
    __system_property_get("ro.build.version.sdk", value);
    if (strcmp(value, "36") || uname(&kernel) ||
        strcmp(kernel.release, "6.12.30-android16-5-pd30ff70-abogkiS948QOPS1AZHL-4k"))
        return 0;
    char enforcing = 0;
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    ssize_t size = fd >= 0 ? read(fd, &enforcing, 1) : -1;
    if (fd >= 0)
        close(fd);
    return size == 1 && enforcing == '1' &&
           pinned_digest("/apex/com.android.virt/lib64/libgfxstream_backend.so",
                         "41414d640a526f56ce9a151f786e965a6e1fe3769e201790e0e58e7a31ff87da");
}

static int artifacts(uid_t uid, int install) {
    if (install && owned_directory(RUNTIME, 1))
        return -1;
    for (size_t i = 0; i < sizeof(gpu4g_artifacts) / sizeof(gpu4g_artifacts[0]); i++) {
        const struct gpu4g_artifact *item = &gpu4g_artifacts[i];
        char source[256], destination[256];
        snprintf(source, sizeof(source), FILES "/runtime/%s", item->asset);
        snprintf(destination, sizeof(destination), RUNTIME "/%s", item->installed);
        struct stat st;
        if (lstat(source, &st) || !S_ISREG(st.st_mode) || st.st_uid != uid || st.st_nlink != 1 ||
            (st.st_mode & 077) || !pinned_digest(source, item->sha256))
            return -1;
        if (install && owned_install_pinned(source, destination, item->sha256, 0500, uid))
            return -1;
    }
    return 0;
}

static int profile(void) {
    const char marker[] = "plb-android17-gpu4096-v1\n";
    struct stat st;
    if (!lstat(PROFILE, &st)) {
        if (owned_directory(PROFILE, 0) || !private_file(PROFILE "/.ready"))
            return -1;
        int fd = open(PROFILE "/.ready", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        char text[sizeof(marker)];
        ssize_t size = fd >= 0 ? read(fd, text, sizeof(text)) : -1;
        if (fd >= 0)
            close(fd);
        return size == sizeof(marker) - 1 && !memcmp(text, marker, sizeof(marker) - 1) ? 0 : -1;
    }
    if (errno != ENOENT || owned_directory(PROFILE ".preparing", 1))
        return -1;
    const char *names[] = {"android-work.qcow2", "android-misc.raw", "android-frp.raw"};
    unsigned long long required = 256ULL * 1024 * 1024;
    for (size_t i = 0; i < 3; i++) {
        char path[256];
        snprintf(path, sizeof(path), ORIGINAL "/%s", names[i]);
        if (!private_file(path) || lstat(path, &st) || st.st_size <= 0)
            return -1;
        required += (unsigned long long)st.st_size;
    }
    struct statvfs space;
    if (statvfs(FILES, &space) || (unsigned long long)space.f_bavail * space.f_frsize < required)
        return -1;
    /* A failed preparation remains visible for diagnosis; do not recursively
     * delete a directory or overwrite an existing partial/original disk. */
    for (size_t i = 0; i < 3; i++) {
        char source[256], destination[256];
        snprintf(source, sizeof(source), ORIGINAL "/%s", names[i]);
        snprintf(destination, sizeof(destination), PROFILE ".preparing/%s", names[i]);
        if (owned_copy(source, destination, 0600, 0))
            return -1;
    }
    const char *ramdisk = PROFILE ".preparing/android-lowram.cpio";
    if (!pinned_digest(ORIGINAL "/android-lowram.cpio",
                       "7e0c04dd3f3b9b92398c1e5ac0161e93d78c450e857d530d55012a6a550315e2") ||
        owned_copy(ORIGINAL "/android-lowram.cpio", ramdisk, 0400, 0))
        return -1;
    int tail = open(RUNTIME "/ramdisk-tail.cpio", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int merged = open(ramdisk, O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
    if (tail < 0 || merged < 0) {
        if (tail >= 0)
            close(tail);
        if (merged >= 0)
            close(merged);
        return -1;
    }
    unsigned char bytes[65536];
    int merge_failed = 0;
    ssize_t count;
    while ((count = read(tail, bytes, sizeof(bytes))) > 0) {
        if (owned_write_all(merged, bytes, (size_t)count)) {
            merge_failed = 1;
            break;
        }
    }
    merge_failed |= count < 0 || fsync(merged) != 0;
    close(tail);
    close(merged);
    if (merge_failed ||
        !pinned_digest(ramdisk, "4ef88a3ce9fdda207e0bfabe1d28ae24319848dcedf613d60ba61e0936f620d5"))
        return -1;
    int fd = open(PROFILE ".preparing/.ready", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                  0600);
    if (fd < 0)
        return -1;
    int failed =
        owned_write_all(fd, (const unsigned char *)marker, sizeof(marker) - 1) || fsync(fd);
    close(fd);
    fd = open(PROFILE ".preparing", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    failed |= fsync(fd) != 0;
    close(fd);
    if (failed || rename(PROFILE ".preparing", PROFILE))
        return -1;
    fd = open(FILES "/android-guests", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    failed = fsync(fd);
    close(fd);
    return failed ? -1 : 0;
}

int main(int argc, char **argv) {
    int check = argc == 2 && !strcmp(argv[1], "--check");
    struct stat app;
    if (getuid() || lstat(FILES, &app) || !S_ISDIR(app.st_mode) || app.st_uid < 10000 ||
        app.st_uid > 19999)
        return 20;
    if (!check && (argc != 3 || strcmp(argv[2], "--ack-six-shmem") || strlen(argv[1]) != 5 ||
                   strspn(argv[1], "0123456789") != 5 || strtoul(argv[1], NULL, 10) != app.st_uid))
        return 20;
    if (!platform())
        return 39;
    if (!plb_host_memory_healthy())
        return 42;
    /* Reject redirected profile parents before any root-owned copy or write. */
    if (owned_directory(FILES "/android-guests", 0) || owned_directory(ORIGINAL, 0))
        return 21;
    if (artifacts(app.st_uid, 0))
        return 31;
    if (!private_file(ORIGINAL "/android-base.qcow2") ||
        !private_file(ORIGINAL "/android-work.qcow2") ||
        !private_file(ORIGINAL "/android-misc.raw") || !private_file(ORIGINAL "/android-frp.raw") ||
        !private_file(ORIGINAL "/android-lowram.cpio") ||
        !pinned_digest(ORIGINAL "/android-lowram.cpio",
                       "7e0c04dd3f3b9b92398c1e5ac0161e93d78c450e857d530d55012a6a550315e2"))
        return 21;
    if (check) {
        puts("ANDROID_PROFILE_CHECK_OK memory_mib=4096");
        return 0;
    }
    umask(077);
    int lock = profile_lock();
    if (lock < 0)
        return 28;
    if (artifacts(app.st_uid, 1))
        return 31;
    if (profile())
        return 35;
    char descriptor[16];
    snprintf(descriptor, sizeof(descriptor), "%d", lock);
    if (fcntl(lock, F_SETFD, 0) || setenv("PLB_DIAGNOSTIC_OWNER_FD", descriptor, 1))
        return 41;
    unsetenv("LD_PRELOAD");
    unsetenv("LD_LIBRARY_PATH");
    execl(RUNTIME "/policy", "policy", "--apply-approved-medium-shmem", "--android4g",
          "--prepare-memory", argv[1], (char *)NULL);
    return 26;
}
