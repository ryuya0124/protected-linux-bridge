/* MIT. Load only the owned backend and inspect symbols; never call GPU/VM/Binder operations. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *prefix = "/data/local/tmp/plb-display-load.";
    if (argc != 2 || strncmp(argv[1], prefix, strlen(prefix)) != 0 || strstr(argv[1], "..") ||
        strlen(argv[1]) > 200) {
        return 40;
    }
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fputs("OWNED_DISPLAY_LOAD_REFUSED\n", stderr);
        return 41;
    }
    const char *symbols[] = {
        "plb_owned_display_configure",     "create_android_display_context",
        "destroy_android_display_context", "create_android_surface",
        "destroy_android_surface",         "get_android_surface_buffer",
        "post_android_surface_buffer",     "set_android_surface_position",
        "android_display_flip_to",
    };
    int result = 0;
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        if (!dlsym(library, symbols[i]))
            result = 42;
    }
    /* Lookup only: no Binder pool, display publication, handle import or GPU allocation. */
    int pool = dlsym(library, "ABinderProcess_startThreadPool") != NULL;
    int ahb = dlsym(library, "AHardwareBuffer_createFromHandle") != NULL;
    printf("OWNED_DISPLAY_LINKAGE abi=%d binder_pool=%d ahb_clone=%d\n", result == 0, pool, ahb);
    dlclose(library);
    return result ? result : (pool && ahb ? 0 : 43);
}
