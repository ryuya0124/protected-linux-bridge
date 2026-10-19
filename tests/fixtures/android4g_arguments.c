/* MIT. Inspect the real serialized VMM arguments without executing any VM. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define VMM "/unused/vmm"
#define PLB_ANDROID4G_BASE "/unused/profile"
#define INITRAMFS "/unused/ramdisk"
#define GUEST_KERNEL "/unused/kernel"
#include "android4g_arguments.h"

int main(void) {
    for (size_t i = 0; android4g_arguments[i]; i++) {
        if (!strcmp(android4g_arguments[i], "--swiotlb") ||
            !strcmp(android4g_arguments[i], "--mem") ||
            !strcmp(android4g_arguments[i], "--protected-vm-without-firmware")) {
            puts(android4g_arguments[i]);
            if (strcmp(android4g_arguments[i], "--protected-vm-without-firmware")) {
                if (!android4g_arguments[i + 1])
                    return 1;
                puts(android4g_arguments[++i]);
            }
        }
    }
    return 0;
}
