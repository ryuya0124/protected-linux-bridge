/* MIT. No-VM GLES draw/normal window flush/real AHB export check.
 * A one-pixel readback waits for producer completion in this functional test;
 * this is not an asynchronous/zero-copy performance result. */
#include "backend.h"
#include "frame_buffer.h"
#include "render_thread_info.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include <sys/stat.h>
#include <fcntl.h>
#include "profile_lock.h"
#include "host_memory_health.h"
#include <android/hardware_buffer.h>
#include <jni.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <atomic>
#include <unistd.h>
using gfxstream::host::FrameBuffer;
using gfxstream::host::GfxstreamFormat;
namespace {
int maintenance = -1, owner = -1;
AndroidDisplayContext *endpoint = nullptr;
AndroidDisplaySurface *surface = nullptr;
AHardwareBuffer *buffer = nullptr;
uint32_t resource = 0;
uint32_t guestContext = 0, guestSurface = 0;
std::unique_ptr<gfxstream::host::RenderThreadInfo> threadInfo;
bool failed = false, initialized = false;
std::atomic<bool> sawAdreno{false};
void error(const char *text) {
    fprintf(stderr, "OWNED_RENDERER_ERROR %s\n", text);
    failed = true;
}
void fence(void *, stream_renderer_fence *) {
}
void debug(void *, stream_renderer_debug *message) {
    if (message && message->message) {
        fprintf(stderr, "RENDERER_LOG %.800s\n", message->message);
        if (strstr(message->message, "Graphics Adapter ") && strstr(message->message, "Adreno"))
            sawAdreno.store(true);
    }
}
} // namespace
extern "C" JNIEXPORT jint JNICALL
Java_net_ryuya_1dev_protectedlinux_displayroot_RootDisplayProbe_admit(JNIEnv *, jclass) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (getuid() || !plb_host_memory_healthy())
        return 1;
    maintenance = profile_maintenance_lock(LOCK_SH);
    owner = profile_lock();
    return maintenance < 0 || owner < 0 ? 2 : 0;
}
extern "C" JNIEXPORT jint JNICALL
Java_net_ryuya_1dev_protectedlinux_displayroot_RootDisplayProbe_start(JNIEnv *, jclass) {
    if (owner < 0 || endpoint)
        return 1;
    int cookie = 1;
    const char features[] = "Vulkan:disabled";
    stream_renderer_param parameters[] = {
        {STREAM_RENDERER_PARAM_USER_DATA, reinterpret_cast<uint64_t>(&cookie)},
        {STREAM_RENDERER_PARAM_RENDERER_FLAGS, STREAM_RENDERER_FLAGS_USE_EGL_BIT |
                                                   STREAM_RENDERER_FLAGS_USE_SURFACELESS_BIT |
                                                   STREAM_RENDERER_FLAGS_USE_GLES_BIT},
        {STREAM_RENDERER_PARAM_FENCE_CALLBACK, reinterpret_cast<uint64_t>(fence)},
        {STREAM_RENDERER_PARAM_WIN0_WIDTH, 480},
        {STREAM_RENDERER_PARAM_WIN0_HEIGHT, 800},
        {STREAM_RENDERER_PARAM_DEBUG_CALLBACK, reinterpret_cast<uint64_t>(debug)},
        {STREAM_RENDERER_PARAM_RENDERER_FEATURES, reinterpret_cast<uint64_t>(features)},
    };
    int result = stream_renderer_init(parameters, sizeof(parameters) / sizeof(*parameters));
    if (result)
        return 2;
    initialized = true;
    FrameBuffer *fb = FrameBuffer::getFB();
    // getGlString requires an emulated guest context, which this no-VM test
    // intentionally has not created. Check the renderer's hardware-init report.
    if (!fb || !sawAdreno.load())
        return 3;
    puts("OWNED_SOURCE_RENDERER Adreno verified in hardware initialization report");
    threadInfo = std::make_unique<gfxstream::host::RenderThreadInfo>();
    threadInfo->initGl();
    EGLint attributes[] = {EGL_SURFACE_TYPE,
                           EGL_PBUFFER_BIT,
                           EGL_RENDERABLE_TYPE,
                           EGL_OPENGL_ES2_BIT,
                           EGL_RED_SIZE,
                           8,
                           EGL_GREEN_SIZE,
                           8,
                           EGL_BLUE_SIZE,
                           8,
                           EGL_ALPHA_SIZE,
                           8,
                           EGL_NONE};
    EGLint config = -1;
    if (fb->chooseConfig(attributes, &config, 1) < 1 || config < 0)
        return 9;
    guestContext = fb->createEmulatedEglContext(config, 0, gfxstream::host::gl::GLESApi_2);
    guestSurface = fb->createEmulatedEglWindowSurface(config, 480, 800);
    if (!guestContext || !guestSurface)
        return 10;
    resource = fb->createColorBuffer(480, 800, GfxstreamFormat::R8G8B8A8_UNORM);
    if (!resource || fb->openColorBuffer(resource) != 0)
        return 4;
    if (!fb->setEmulatedEglWindowSurfaceColorBuffer(guestSurface, resource) ||
        !fb->bindContext(guestContext, guestSurface, guestSurface))
        return 11;
    auto exported = fb->exportColorBuffer(resource);
    if (!exported || exported->descriptorInfo.streamHandleType != STREAM_HANDLE_TYPE_PLATFORM_AHB)
        return 5;
    // Keep our own reference after the descriptor's retained owner is released.
    buffer = reinterpret_cast<AHardwareBuffer *>(exported->descriptorInfo.handle);
    if (!buffer)
        return 6;
    AHardwareBuffer_acquire(buffer);
    endpoint = create_android_display_context("owned-renderer-no-vm", error);
    if (!endpoint)
        return 7;
    surface = create_android_surface(endpoint, 480, 800, false);
    return surface && !failed ? 0 : 8;
}
extern "C" JNIEXPORT jint JNICALL
Java_net_ryuya_1dev_protectedlinux_displayroot_RootDisplayProbe_frame(JNIEnv *, jclass,
                                                                      jint color) {
    if (!endpoint || !buffer || failed)
        return 1;
    const unsigned red = (color >> 16) & 255, green = (color >> 8) & 255, blue = color & 255;
    FrameBuffer *fb = FrameBuffer::getFB();
    if (!fb->bindContext(guestContext, guestSurface, guestSurface))
        return 2;
    auto &gl = gfxstream::host::gl::s_gles2;
    gl.glViewport(0, 0, 480, 800);
    gl.glClearColor(red / 255.0f, green / 255.0f, blue / 255.0f, 1.0f);
    gl.glClear(GL_COLOR_BUFFER_BIT);
    gl.glFinish();
    if (gl.glGetError() || !fb->flushEmulatedEglWindowSurfaceColorBuffer(guestSurface))
        return 2;
    unsigned char pixel[4]{};
    // Readback completes prior texture writes before the no-fence test flip.
    fb->readColorBuffer(resource, 240, 400, 1, 1, GfxstreamFormat::R8G8B8A8_UNORM, pixel, 4);
    if (pixel[0] != red || pixel[1] != green || pixel[2] != blue || pixel[3] != 255)
        return 3;
    AHardwareBuffer_Desc desc{};
    AHardwareBuffer_describe(buffer, &desc);
    using Handle = const int *(*)(const AHardwareBuffer *);
    auto get = reinterpret_cast<Handle>(dlsym(RTLD_DEFAULT, "AHardwareBuffer_getNativeHandle"));
    if (!get)
        return 3;
    const int *handle = get(buffer);
    if (!handle || handle[0] != 12 || handle[1] < 1 || handle[1] > 64 || handle[2] < 0 ||
        sizeof(desc) + handle[2] * sizeof(int) > 16384)
        return 4;
    std::vector<uint8_t> metadata(sizeof(desc) + handle[2] * sizeof(int));
    memcpy(metadata.data(), &desc, sizeof(desc));
    memcpy(metadata.data() + sizeof(desc), handle + 3 + handle[1], handle[2] * sizeof(int));
    AHardwareBufferInfo info{static_cast<size_t>(handle[1]), handle + 3, metadata.size(),
                             metadata.data()};
    android_display_flip_to(endpoint, surface, &info);
    return failed ? 5 : 0;
}
extern "C" JNIEXPORT void JNICALL
Java_net_ryuya_1dev_protectedlinux_displayroot_RootDisplayProbe_stop(JNIEnv *, jclass) {
    if (endpoint)
        destroy_android_display_context(endpoint);
    endpoint = nullptr;
    FrameBuffer *fb = FrameBuffer::getFB();
    if (fb && threadInfo)
        fb->bindContext(0, 0, 0);
    if (guestSurface && fb)
        fb->destroyEmulatedEglWindowSurface(guestSurface);
    if (guestContext && fb)
        fb->destroyEmulatedEglContext(guestContext);
    guestSurface = guestContext = 0;
    if (buffer)
        AHardwareBuffer_release(buffer);
    buffer = nullptr;
    if (resource && FrameBuffer::getFB())
        FrameBuffer::getFB()->closeColorBuffer(resource);
    resource = 0;
    threadInfo.reset();
    if (initialized)
        stream_renderer_teardown();
    initialized = false;
    if (owner >= 0)
        close(owner);
    if (maintenance >= 0)
        close(maintenance);
    owner = maintenance = -1;
}
