/* MIT. Offline snapshot for a separate Surface experiment; originals are never written. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#define SOURCE FILES "/android-guests/cf17-gpu4096"
#ifdef PLB_ANDROID4G_VULKAN_DIAGNOSTIC
#define TARGET FILES "/android-guests/cf17-vulkan4096"
#define RUNTIME FILES "/android-vulkan4g-runtime"
#define COPY_PREFIX "vulkan-"
#define COPY_MARKER "plb-vulkan-offline-copy-v1\n"
#else
#define TARGET FILES "/android-guests/cf17-gpu4096-surface"
#define RUNTIME FILES "/android-gpu4g-surface-runtime"
#define COPY_PREFIX "surface-"
#define COPY_MARKER "plb-surface-offline-copy-v1\n"
#endif
#include "../native/profile_lock.h"
#include "../native/android_gpu4g_files.h"
#include "host_memory_health.h"
struct copy_item {
    const char *name, *sha256;
};
#include "android_surface_profile.generated.h"

int main(int argc, char **argv) {
    int runtime_only = argc == 2 && !strcmp(argv[1], "--runtime-only");
    if (getuid() || (argc != 1 && !runtime_only) || !plb_host_memory_healthy())
        return 20;
    int lock = profile_lock();
    if (lock < 0)
        return 28;
    if (owned_directory(FILES "/android-guests", 0) || owned_directory(SOURCE, 0) ||
        owned_directory(RUNTIME, 1))
        return 21;
    if (runtime_only) {
        if (owned_directory(TARGET, 0) || !private_file(TARGET "/.ready"))
            return 22;
    } else {
        struct stat info;
        if (!lstat(TARGET, &info) || errno != ENOENT || !lstat(TARGET ".preparing", &info) ||
            errno != ENOENT)
            return 22;
        unsigned long long required = 128ULL * 1024 * 1024;
        for (size_t i = 0; i < sizeof(profile_items) / sizeof(*profile_items); i++) {
            char source[256];
            snprintf(source, sizeof(source), SOURCE "/%s", profile_items[i].name);
            if (!private_file(source) || lstat(source, &info) || info.st_size <= 0 ||
                !pinned_digest(source, profile_items[i].sha256))
                return 23;
            required += (unsigned long long)info.st_size;
        }
        struct statvfs space;
        if (statvfs(FILES, &space) ||
            (unsigned long long)space.f_bavail * space.f_frsize < required)
            return 24;
        if (owned_directory(TARGET ".preparing", 1))
            return 25;
        for (size_t i = 0; i < sizeof(profile_items) / sizeof(*profile_items); i++) {
            char source[256], target[256];
            snprintf(source, sizeof(source), SOURCE "/%s", profile_items[i].name);
            snprintf(target, sizeof(target), TARGET ".preparing/%s", profile_items[i].name);
            if (owned_copy(source, target, 0600, 0) ||
                !pinned_digest(target, profile_items[i].sha256) ||
                !pinned_digest(source, profile_items[i].sha256))
                return 26;
        }
    }
    for (size_t i = 0; i < sizeof(runtime_items) / sizeof(*runtime_items); i++) {
        char source[256], target[256];
        snprintf(source, sizeof(source), FILES "/kernel-probes/" COPY_PREFIX "%s",
                 runtime_items[i].name);
        snprintf(target, sizeof(target), RUNTIME "/%s", runtime_items[i].name);
        if (owned_install_pinned(source, target, runtime_items[i].sha256, 0500, 0))
            return 27;
    }
    if (owned_install_pinned(FILES "/android-gpu4g-runtime/guest.Image", RUNTIME "/guest.Image",
                             "4ae0f63515fe8a16945007b26423c0a6eccb8b04119259bd424d5e15894b52f5",
                             0500, 0))
        return 29;
    if (runtime_only) {
        close(lock);
        puts("PLB_SURFACE_RUNTIME_UPDATED_OFFLINE");
        return 0;
    }
    int marker = open(TARGET ".preparing/.ready",
                      O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    const char text[] = COPY_MARKER;
    if (marker < 0 || owned_write_all(marker, (const unsigned char *)text, sizeof(text) - 1) ||
        fsync(marker))
        return 30;
    close(marker);
    int pending = open(TARGET ".preparing", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (pending < 0 || fsync(pending))
        return 31;
    close(pending);
    if (!plb_host_memory_healthy() || rename(TARGET ".preparing", TARGET))
        return 31;
    int parent = open(FILES "/android-guests", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (parent < 0 || fsync(parent))
        return 32;
    close(parent);
    close(lock);
    puts("PLB_SURFACE_OFFLINE_COPY_VERIFIED");
    return 0;
}
