/* MIT. Inspect the real profile paths/flags without opening a host or guest file. */
#include <stdio.h>
#define FILES "/owned/app"
#include "android4g_config.h"
#define VMM PLB_ANDROID4G_VMM
#define INITRAMFS PLB_ANDROID4G_BASE "/android-lowram.cpio"
#define GUEST_KERNEL PLB_ANDROID4G_KERNEL
#include "android4g_arguments.h"
int main(void) {
    for (size_t i = 0; android4g_arguments[i]; i++)
        puts(android4g_arguments[i]);
    return 0;
}
