/* MIT. No-VM GLES producer using real AHB export/import and the owned Binder backend. */
#include "backend.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "profile_lock.h"

namespace {
int maintenance = -1, owner = -1;
EGLDisplay display = EGL_NO_DISPLAY;
EGLContext context = EGL_NO_CONTEXT;
EGLSurface pbuffer = EGL_NO_SURFACE;
EGLImageKHR image = EGL_NO_IMAGE_KHR;
AHardwareBuffer *buffer = nullptr;
AndroidDisplayContext *endpoint = nullptr;
AndroidDisplaySurface *surface = nullptr;
GLuint texture = 0, framebuffer = 0;
bool failed = false;
void error(const char *message) {
    fprintf(stderr, "%s\n", message);
    failed = true;
}
} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_net_ryuya_1dev_protectedlinux_displayroot_RootDisplayProbe_admit(JNIEnv *, jclass) {
    if (getuid())
        return 1;
    FILE *health = fopen("/proc/sys/kernel/tainted", "re");
    unsigned taint = 0;
    if (!health)
        return 2;
    int parsed = fscanf(health, "%u", &taint);
    fclose(health);
    if (parsed != 1 || (taint & ((1u << 5) | (1u << 7))))
        return 3;
    maintenance = profile_maintenance_lock(LOCK_SH);
    owner = profile_lock();
    return maintenance < 0 || owner < 0 ? 4 : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_net_ryuya_1dev_protectedlinux_displayroot_RootDisplayProbe_start(JNIEnv *, jclass) {
    if (owner < 0 || endpoint)
        return 1;
    endpoint = create_android_display_context("owned-no-vm-probe", error);
    if (!endpoint)
        return 2;
    surface = create_android_surface(endpoint, 480, 800, false);
    if (!surface)
        return 3;
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
        return 4;
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
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1)
        return 5;
    EGLint dimensions[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    pbuffer = eglCreatePbufferSurface(display, config, dimensions);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
    if (pbuffer == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, pbuffer, pbuffer, context))
        return 6;
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    if (!renderer || !strstr(renderer, "Adreno"))
        return 7;
    printf("OWNED_GLES_RENDERER %s\n", renderer);
    AHardwareBuffer_Desc desc{};
    desc.width = 480;
    desc.height = 800;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                 AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
    if (AHardwareBuffer_allocate(&desc, &buffer))
        return 8;
    auto client = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
        eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    auto create =
        reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    auto target = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!client || !create || !target)
        return 9;
    EGLint preserved[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    image = create(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client(buffer), preserved);
    if (image == EGL_NO_IMAGE_KHR)
        return 10;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    target(GL_TEXTURE_2D, image);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? 0 : 11;
}

extern "C" JNIEXPORT jint JNICALL
Java_net_ryuya_1dev_protectedlinux_displayroot_RootDisplayProbe_frame(JNIEnv *, jclass,
                                                                      jint color) {
    if (!endpoint || !buffer || failed)
        return 1;
    const unsigned red = (color >> 16) & 255, green = (color >> 8) & 255, blue = color & 255;
    glViewport(0, 0, 480, 800);
    glClearColor(red / 255.0f, green / 255.0f, blue / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Qualification only: finish producer writes before publishing with fence -1.
    // This is not a proof of the VM producer's asynchronous fence ordering.
    glFinish();
    unsigned char pixel[4]{};
    glReadPixels(240, 400, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (glGetError() || pixel[0] != red || pixel[1] != green || pixel[2] != blue || pixel[3] != 255)
        return 2;
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
    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
        if (framebuffer)
            glDeleteFramebuffers(1, &framebuffer);
        if (texture)
            glDeleteTextures(1, &texture);
        auto destroy =
            reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
        if (destroy && image != EGL_NO_IMAGE_KHR)
            destroy(display, image);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        if (pbuffer != EGL_NO_SURFACE)
            eglDestroySurface(display, pbuffer);
        eglTerminate(display);
    }
    if (buffer)
        AHardwareBuffer_release(buffer);
    if (owner >= 0)
        close(owner);
    if (maintenance >= 0)
        close(maintenance);
    owner = maintenance = -1;
}
