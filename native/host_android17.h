/* MIT. Exact ROM admission for the dedicated Android 17 branch.
 * Static identities do not establish Gunyah runtime or guest GPU compatibility. */
#ifndef PLB_HOST_ANDROID17_H
#define PLB_HOST_ANDROID17_H
#include <sys/system_properties.h>
#include <sys/utsname.h>

static int host17_digest(const char *path, const char *expected) {
    int descriptors[2];
    if (pipe2(descriptors, O_CLOEXEC))
        return 0;
    pid_t child = fork();
    if (!child) {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0)
            _exit(126);
        close(descriptors[1]);
        execl("/system/bin/toybox", "toybox", "sha256sum", path, (char *)NULL);
        _exit(127);
    }
    close(descriptors[1]);
    char output[256];
    ssize_t size = child > 0 ? read(descriptors[0], output, sizeof(output)) : -1;
    close(descriptors[0]);
    int status = -1;
    if (child > 0)
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
    return size > 64 && output[64] == ' ' && !memcmp(output, expected, 64) && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
}

static int host17_admitted(void) {
    char value[PROP_VALUE_MAX];
    struct utsname kernel;
    __system_property_get("ro.product.model", value);
    if (strcmp(value, "SM-S948Q"))
        return 0;
    __system_property_get("ro.build.version.sdk", value);
    if (strcmp(value, "37"))
        return 0;
    __system_property_get("ro.build.version.incremental", value);
    if (strcmp(value, "S948QOPU1BZI9") || uname(&kernel) ||
        strcmp(kernel.release, "6.12.69-android16-6-pee899be-abogkiS948QOPU1BZI9-4k"))
        return 0;
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    char enforcing = 0;
    ssize_t size = fd >= 0 ? read(fd, &enforcing, 1) : -1;
    if (fd >= 0)
        close(fd);
    return size == 1 && enforcing == '1' &&
           host17_digest("/apex/com.android.virt/bin/crosvm",
                         "a0cfa5b957d119202b85a069928b3b56bd080329efed42eb83b182c9513885e9") &&
           host17_digest("/apex/com.android.virt/etc/fs/microdroid_kernel",
                         "02f0044263e79ae95f321325ca1741d092f0c78c693b8114830d6e1ff2849aae");
}
#endif
