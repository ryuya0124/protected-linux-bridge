/* MIT. On-device mock only: regular files, no sysfs or VM execution. */
#include "diagnostic_owner_lock.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    int lock = diagnostic_owner_lock();
    if (lock < 0)
        return 40;
    const char *paths[] = {
        "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/policy-mock/small-16k",
        "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/policy-mock/small-32k",
        "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/policy-mock/small-64k",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        char value[32] = {0};
        int fd = open(paths[i], O_RDONLY | O_CLOEXEC);
        if (fd < 0 || read(fd, value, sizeof(value) - 1) <= 0 || strcmp(value, "advise"))
            return 41;
        close(fd);
    }
    puts("PLB_MOCK_CHILD_INHERITED_LOCK_AND_SAW_ALL_THREE_SETTINGS");
    close(lock);
    return 0;
}
