/* MIT. Developer comparison: same VMM/guest/GLES, without ART or display publication. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#include "android17_config.generated.h"
#define VMM PLB_ANDROID4G_VMM
#define INITRAMFS PLB_ANDROID4G_BASE "/android-lowram.cpio"
#define GUEST_KERNEL PLB_ANDROID4G_KERNEL
#include "android17_arguments.generated.h"
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    size_t expected = sizeof(android4g_arguments) / sizeof(*android4g_arguments) - 1;
    if (getuid() || argc != (int)expected)
        return 20;
    for (size_t i = 0; i < expected; i++)
        if (strcmp(argv[i], android4g_arguments[i]))
            return 21;
    printf("ANDROID17_NATIVE_ENTRY thp_disabled=%d\n", prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0));
    void *library = dlopen(PLB_ANDROID4G_RUNTIME "/libplb_vmm_android17.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "VMM_LOAD_REFUSED %s\n", dlerror());
        return 22;
    }
    int (*entry)(int, const char *const *) = dlsym(library, "plb_crosvm_owned_display_main");
    if (!entry)
        return 23;
    int result = entry(argc, (const char *const *)argv);
    printf("ANDROID17_NATIVE_VMM_EXIT %d\n", result);
    return result;
}
