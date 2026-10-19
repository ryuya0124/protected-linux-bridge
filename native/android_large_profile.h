/* MIT. The explicitly selected, pinned Android RAM profiles.
 * Copy only offline owned regular files; retain the normal 512 MiB profile.
 * No kernel, partition, mount, SELinux or unrelated process changes. */
#ifndef PLB_ANDROID_LARGE_PROFILE_H
#define PLB_ANDROID_LARGE_PROFILE_H
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include "../tools/android_large_config.h"
#if PLB_ANDROID_LARGE_MEMORY_MIB == 1792
#include "android_large1792_build.generated.h"
#else
#include "android_large_build.generated.h"
#endif
#include "small_shmem_guard.h"
#include "android_owned_process.h"
#include "../tools/allocation_probe_limits.h"
#include "../tools/host_memory_health.h"

#define PLB_LARGE_RUNTIME FILES "/android-runtime"
#define PLB_LARGE_VMM PLB_LARGE_RUNTIME "/crosvm"
#define PLB_LARGE_LIBRARY PLB_LARGE_RUNTIME "/" PLB_LARGE_LIBRARY_NAME
#define PLB_LARGE_LIBRARY_SOURCE FILES "/runtime/" PLB_LARGE_LIBRARY_NAME
#define PLB_VMM_DIGEST "9b4d56db70be1d3d646a08da0cb3e6eb95637457e5f15cb3b1c627f348e3e847"

static int large_platform(void) {
    char value[PROP_VALUE_MAX];
    struct utsname kernel;
    __system_property_get("ro.product.model", value);
    if (strcmp(value, "SM-S948Q"))
        return 0;
    __system_property_get("ro.build.version.sdk", value);
    if (strcmp(value, "36"))
        return 0;
    __system_property_get("ro.build.version.incremental", value);
    if (strcmp(value, "S948QOPS1AZHL") || uname(&kernel) ||
        strcmp(kernel.release, "6.12.30-android16-5-pd30ff70-abogkiS948QOPS1AZHL-4k"))
        return 0;
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
    char enforcing = 0;
    ssize_t count = fd >= 0 ? read(fd, &enforcing, 1) : -1;
    if (fd >= 0)
        close(fd);
    return count == 1 && enforcing == '1' &&
           pinned_digest("/apex/com.android.virt/bin/crosvm", PLB_VMM_DIGEST);
}

static int large_private_directory(const char *path, int create) {
    if (create && mkdir(path, 0700) && errno != EEXIST)
        return -1;
    struct stat info;
    return lstat(path, &info) || !S_ISDIR(info.st_mode) || info.st_uid || (info.st_mode & 077) ? -1
                                                                                               : 0;
}

static int large_write_all(int fd, const unsigned char *data, size_t size) {
    size_t at = 0;
    while (at < size) {
        ssize_t written = write(fd, data + at, size - at);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        at += (size_t)written;
    }
    return 0;
}

static int large_copy(const char *source, const char *destination, mode_t mode, uid_t source_uid) {
    int input = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0)
        return -1;
    struct stat before = {0}, after = {0};
    int valid =
        !fstat(input, &before) && S_ISREG(before.st_mode) && before.st_uid == source_uid &&
        before.st_nlink == 1 &&
        !(before.st_mode & (!strcmp(source, "/apex/com.android.virt/bin/crosvm") ? 022 : 077));
    int output =
        valid ? open(destination, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode) : -1;
    if (output < 0) {
        close(input);
        return -1;
    }
    unsigned char bytes[65536], comparison[65536];
    off_t offset = 0;
    int failed = 0;
    for (;;) {
        ssize_t count = pread(input, bytes, sizeof(bytes), offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            failed = 1;
            break;
        }
        if (!count)
            break;
        if (large_write_all(output, bytes, (size_t)count) ||
            pread(output, comparison, (size_t)count, offset) != count ||
            memcmp(bytes, comparison, (size_t)count)) {
            failed = 1;
            break;
        }
        offset += count;
    }
    failed |= fstat(input, &after) != 0;
    failed |= offset != before.st_size || before.st_size != after.st_size ||
              before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
              before.st_mtim.tv_nsec != after.st_mtim.tv_nsec;
    failed |= fsync(output) != 0 || fchmod(output, mode) != 0;
    close(input);
    close(output);
    if (failed)
        unlink(destination); /* Only the new file created by this invocation. */
    return failed ? -1 : 0;
}

static int large_install_pinned(const char *source, const char *destination, const char *digest,
                                mode_t mode, uid_t source_uid) {
    if (private_file(destination) && pinned_digest(destination, digest))
        return 0;
    char pending[256];
    snprintf(pending, sizeof(pending), "%s.pending", destination);
    struct stat info;
    if (!lstat(pending, &info)) {
        if (!private_file(pending) || unlink(pending))
            return -1;
    } else if (errno != ENOENT)
        return -1;
    if (large_copy(source, pending, mode, source_uid) || !pinned_digest(pending, digest))
        return -1;
    if (!lstat(destination, &info) && !private_file(destination))
        return -1;
    return rename(pending, destination);
}

static int large_profile_files(uid_t app_uid) {
    if (large_private_directory(FILES "/android-guests", 0) ||
        large_private_directory(PLB_LARGE_RUNTIME, 1) ||
        large_install_pinned("/apex/com.android.virt/bin/crosvm", PLB_LARGE_VMM, PLB_VMM_DIGEST,
                             0500, 0) ||
        large_install_pinned(PLB_LARGE_LIBRARY_SOURCE, PLB_LARGE_LIBRARY, PLB_LARGE_LIBRARY_SHA256,
                             0400, app_uid))
        return -1;
    struct stat info;
    if (!lstat(BASE, &info)) {
        if (large_private_directory(BASE, 0) || !private_file(BASE "/.ready"))
            return -1;
        int ready = open(BASE "/.ready", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        char marker[32] = {0};
        ssize_t length = ready >= 0 ? read(ready, marker, sizeof(marker)) : -1;
        if (ready >= 0)
            close(ready);
        const char expected[] = PLB_LARGE_READY_MARKER;
        return length == (ssize_t)(sizeof(expected) - 1) &&
                       !memcmp(marker, expected, sizeof(expected) - 1)
                   ? 0
                   : -1;
    }
    if (errno != ENOENT)
        return -1;
    const char *names[] = {"android-work.qcow2", "android-misc.raw", "android-frp.raw",
                           "android-lowram.cpio", ".ready"};
    unsigned long long required = 512ULL * PLB_PROBE_MIB;
    for (size_t i = 0; i < 4; i++) {
        char source[256];
        snprintf(source, sizeof(source), FILES "/android-guests/cuttlefish17/%s", names[i]);
        if (lstat(source, &info) || !private_file(source) || info.st_size < 0)
            return -1;
        required += (unsigned long long)info.st_size;
    }
    struct statvfs space;
    if (statvfs(FILES, &space) || (unsigned long long)space.f_bavail * space.f_frsize < required)
        return -1;
    const char *pending = BASE ".preparing";
    if (large_private_directory(pending, 1))
        return -1;
    DIR *directory_entries = opendir(pending);
    if (!directory_entries)
        return -1;
    struct dirent *entry;
    int unknown = 0;
    while ((entry = readdir(directory_entries))) {
        int known = !strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..");
        for (size_t i = 0; i < 5; i++)
            known |= !strcmp(entry->d_name, names[i]);
        unknown |= !known;
    }
    closedir(directory_entries);
    if (unknown)
        return -1;
    /* Recover only named, private files from an interrupted preparation. */
    for (size_t i = 0; i < 5; i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", pending, names[i]);
        if (!lstat(path, &info) && (!private_file(path) || unlink(path)))
            return -1;
    }
    for (size_t i = 0; i < 4; i++) {
        char source[256], destination[256];
        snprintf(source, sizeof(source), FILES "/android-guests/cuttlefish17/%s", names[i]);
        snprintf(destination, sizeof(destination), "%s/%s", pending, names[i]);
        if (large_copy(source, destination, i == 3 ? 0400 : 0600, 0))
            return -1;
    }
    int ready =
        open(BASE ".preparing/.ready", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    const char marker[] = PLB_LARGE_READY_MARKER;
    if (ready < 0)
        return -1;
    int failed =
        large_write_all(ready, (const unsigned char *)marker, sizeof(marker) - 1) || fsync(ready);
    close(ready);
    int pending_directory = open(pending, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (pending_directory < 0)
        return -1;
    failed |= fsync(pending_directory) != 0;
    close(pending_directory);
    if (failed || rename(pending, BASE))
        return -1;
    int directory = open(BASE, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0)
        return -1;
    failed = fsync(directory);
    close(directory);
    directory = open(FILES "/android-guests", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0)
        return -1;
    failed |= fsync(directory) != 0;
    close(directory);
    return failed ? -1 : 0;
}
#endif
