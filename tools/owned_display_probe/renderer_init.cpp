/* MIT. Bounded, no-VM initialization of the separate GLES-only renderer. */
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "profile_lock.h"
#include "host_memory_health.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
static void fence(void *, stream_renderer_fence *) {
}
static void debug(void *, stream_renderer_debug *message) {
    if (message && message->message)
        fprintf(stderr, "RENDERER_LOG %.800s\n", message->message);
}
int main(void) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (getuid() || !plb_host_memory_healthy())
        return 40;
    int maintenance = profile_maintenance_lock(LOCK_SH), owner = profile_lock();
    if (maintenance < 0 || owner < 0)
        return 41;
    int cookie = 1;
    const char features[] = "Vulkan:disabled";
    stream_renderer_param parameters[] = {
        {STREAM_RENDERER_PARAM_USER_DATA, reinterpret_cast<uint64_t>(&cookie)},
        {STREAM_RENDERER_PARAM_RENDERER_FLAGS, STREAM_RENDERER_FLAGS_USE_EGL_BIT |
                                                   STREAM_RENDERER_FLAGS_USE_SURFACELESS_BIT |
                                                   STREAM_RENDERER_FLAGS_USE_GLES_BIT},
        {STREAM_RENDERER_PARAM_FENCE_CALLBACK, reinterpret_cast<uint64_t>(fence)},
        {STREAM_RENDERER_PARAM_WIN0_WIDTH, 64},
        {STREAM_RENDERER_PARAM_WIN0_HEIGHT, 64},
        {STREAM_RENDERER_PARAM_DEBUG_CALLBACK, reinterpret_cast<uint64_t>(debug)},
        {STREAM_RENDERER_PARAM_RENDERER_FEATURES, reinterpret_cast<uint64_t>(features)},
    };
    puts("ANDROID17_GLES_RENDERER_INIT_BEGIN vulkan_requested=0");
    int result = stream_renderer_init(parameters, sizeof(parameters) / sizeof(*parameters));
    printf("ANDROID17_GLES_RENDERER_INIT_RESULT %d\n", result);
    if (!result) {
        stream_renderer_teardown();
        puts("ANDROID17_GLES_RENDERER_TEARDOWN_PASS");
    }
    close(owner);
    close(maintenance);
    return result ? 42 : 0;
}
