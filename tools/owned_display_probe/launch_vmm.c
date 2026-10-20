/* MIT. Exact fixed developer argv only; keeps the validated RAM FD and stdin capability. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#include "android17_config.generated.h"
#define VMM PLB_ANDROID4G_VMM
#define INITRAMFS PLB_ANDROID4G_BASE "/android-lowram.cpio"
#define GUEST_KERNEL PLB_ANDROID4G_KERNEL
#include "android17_arguments.generated.h"
#ifndef PLB_OWNED_VMM_JAR
#define PLB_OWNED_VMM_JAR PLB_ANDROID4G_RUNTIME "/owned-vmm.jar"
#endif
int main(int argc, char **argv) {
    size_t expected = sizeof(android4g_arguments) / sizeof(*android4g_arguments) - 1;
    if (getuid() || argc != (int)expected)
        return 20;
    for (size_t i = 0; i < expected; i++)
        if (strcmp(argv[i], android4g_arguments[i]))
            return 21;
    if (setenv("CLASSPATH", PLB_OWNED_VMM_JAR, 1))
        return 22;
    char *java[168] = {"/system/bin/app_process", "-Xmx64m", "/system/bin",
                       "net.ryuya_dev.protectedlinux.displayroot.RootOwnedVmm"};
    for (size_t i = 0; i < expected; i++)
        java[4 + i] = argv[i];
    execv(java[0], java);
    return 23;
}
