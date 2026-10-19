/* MIT. Experimental process-local compatibility for Android's Vulkan loader.
 * Only the owned pinned diagnostic VMM may load this library. No Android loader,
 * driver, kernel, namespace policy or protection mechanism is modified.
 * Re-query just three global Vulkan commands without an instance when the
 * Android loader returned NULL for the instance-qualified lookup.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <vulkan/vulkan.h>
#define FIND_SYMBOL "_ZNK9gfxstream4base13SharedLibrary10findSymbolEPKc"
static void *(*original_symbol)(void *, const char *);
static PFN_vkGetInstanceProcAddr original_get;
static _Atomic unsigned repaired;
#ifdef PLB_GFXSTREAM_DIAGNOSTIC_LOG
static void (*original_log)(int, const char *, int, const char *, const char *, ...);
#endif

static PFN_vkVoidFunction compatible_get(VkInstance instance, const char *name) {
    PFN_vkVoidFunction result = original_get(instance, name);
    if (!result && instance && name &&
        (!strcmp(name, "vkCreateInstance") ||
         !strcmp(name, "vkEnumerateInstanceExtensionProperties") ||
         !strcmp(name, "vkEnumerateInstanceLayerProperties"))) {
        result = original_get(VK_NULL_HANDLE, name);
        if (result && atomic_fetch_add(&repaired, 1) < 12)
            dprintf(STDERR_FILENO, "PLB_GFXSTREAM_GLOBAL_LOOKUP_REPAIRED %s\n", name);
    }
    return result;
}

__attribute__((constructor)) static void initialize(void) {
    struct stat actual, copy;
#ifdef PLB_ANDROID4G_LOADER_COMPAT
    const char *path =
        "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/android4g-crosvm";
    const char *expected_name = "plb-android17-gpu4096";
#else
    const char *path = "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/crosvm-copy";
    const char *expected_name = "plb-android17-probe";
#endif
    char args[8192] = {0};
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    ssize_t size = fd < 0 ? -1 : read(fd, args, sizeof(args));
    if (fd >= 0)
        close(fd);
    if (getuid() || stat("/proc/self/exe", &actual) || stat(path, &copy) ||
        actual.st_dev != copy.st_dev || actual.st_ino != copy.st_ino || size <= 0 ||
        !memmem(args, (size_t)size, expected_name, strlen(expected_name) + 1))
        _exit(79);
#ifdef PLB_GFXSTREAM_DIAGNOSTIC_LOG
    original_log =
        dlsym(RTLD_NEXT, "_ZN9gfxstream4host4impl12GfxstreamLogENS0_8LogLevelEPKciS4_S4_z");
    if (!original_log)
        _exit(76);
#endif
    original_symbol = dlsym(RTLD_NEXT, FIND_SYMBOL);
    if (!original_symbol)
        _exit(78);
    void *loader = dlopen("/system/lib64/libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    original_get = loader ? dlsym(loader, "vkGetInstanceProcAddr") : NULL;
    if (!original_get)
        _exit(77);
    unsetenv("LD_PRELOAD");
    fputs("PLB_GFXSTREAM_ANDROID_LOADER_COMPAT_READY\n", stderr);
}

void *find_symbol(void *library, const char *name) __asm__(FIND_SYMBOL);
void *find_symbol(void *library, const char *name) {
    if (!original_symbol || !original_get)
        _exit(78);
    void *result = original_symbol(library, name);
    if (result == (void *)original_get && name && !strcmp(name, "vkGetInstanceProcAddr"))
        return (void *)compatible_get;
    return result;
}

#ifdef PLB_GFXSTREAM_DIAGNOSTIC_LOG
/* The exact installed export is verified before this diagnostic build. Keep
 * logging bounded and private; it does not replace loader/driver operations. */
void diagnostic_log(int level, const char *file, int line, const char *function, const char *format,
                    ...) __asm__("_ZN9gfxstream4host4impl12GfxstreamLogENS0_8LogLevelEPKciS4_S4_z");
void diagnostic_log(int level, const char *file, int line, const char *function, const char *format,
                    ...) {
    (void)function;
    static _Atomic unsigned messages;
    unsigned number = atomic_fetch_add(&messages, 1);
    char message[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (number < 500)
        dprintf(STDERR_FILENO, "PLB_GFXSTREAM_LOG level=%d %.180s:%d %.2047s\n", level, file, line,
                message);
    /* Preserve original filtering and, crucially, fatal termination. */
    if (!original_log)
        _exit(76);
    original_log(level, file, line, function, "%s", message);
}
#endif
