/* MIT. Initialize only the private source-built renderer, without a VM.
 * Tests the formerly failing representative ColorBuffer allocation. This is
 * not evidence of guest Vulkan rendering. No global loader/policy changes.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"

#define RENDERER                                                                                   \
    "/data/user/0/net.ryuya_dev.protectedlinux/files/kernel-probes/libgfxstream-vulkan.so"

static void fence(void *cookie, struct stream_renderer_fence *value) {
    (void)cookie;
    (void)value;
}

static void debug(void *cookie, struct stream_renderer_debug *value) {
    (void)cookie;
    static unsigned count;
    if (count++ < 160 && value && value->message)
        fprintf(stderr, "PLB_RENDERER_DEBUG level=%u %.400s\n", value->debug_type, value->message);
}

int main(void) {
    alarm(20);
    setvbuf(stdout, NULL, _IONBF, 0);
    struct stat st;
    if (getuid() || getenv("LD_PRELOAD") || lstat(RENDERER, &st) || !S_ISREG(st.st_mode) ||
        st.st_uid || st.st_nlink != 1 || (st.st_mode & 0222))
        return 20;
    void *library = dlopen(RENDERER, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "PLB_RENDERER_LOAD_FAILED %.400s\n", dlerror());
        return 21;
    }
    int (*initialize)(struct stream_renderer_param *, uint64_t) =
        dlsym(library, "stream_renderer_init");
    void (*teardown)(void) = dlsym(library, "stream_renderer_teardown");
    if (!initialize || !teardown)
        return 22;
    struct stream_renderer_param parameters[] = {
        {STREAM_RENDERER_PARAM_USER_DATA, 0},
        {STREAM_RENDERER_PARAM_RENDERER_FLAGS,
         STREAM_RENDERER_FLAGS_USE_EGL_BIT | STREAM_RENDERER_FLAGS_USE_SURFACELESS_BIT |
             STREAM_RENDERER_FLAGS_USE_GLES_BIT | STREAM_RENDERER_FLAGS_USE_VK_BIT},
        {STREAM_RENDERER_PARAM_FENCE_CALLBACK, (uintptr_t)fence},
        {STREAM_RENDERER_PARAM_DEBUG_CALLBACK, (uintptr_t)debug},
        {STREAM_RENDERER_PARAM_WIN0_WIDTH, 64},
        {STREAM_RENDERER_PARAM_WIN0_HEIGHT, 64},
    };
    int result = initialize(parameters, sizeof(parameters) / sizeof(parameters[0]));
    printf("PLB_RENDERER_INIT result=%d\n", result);
    if (!result) {
        teardown();
        puts("PLB_RENDERER_TEARDOWN_COMPLETE");
    }
    dlclose(library);
    return result ? 23 : 0;
}
