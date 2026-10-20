/* MIT. Offscreen, synthetic GLES YUV test. No camera, screen, Vulkan or host tuning.
 * Build a distinct guest binary with PLB_YUV_GUEST; identity never comes from argv.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef PLB_YUV_GUEST
#include <fcntl.h>
#include <sys/stat.h>
#define FILES "/data/user/0/net.ryuya_dev.protectedlinux/files"
#include "profile_lock.h"
#include "host_memory_health.h"
#endif

#ifdef PLB_YUV_TRANSLATOR
#include "frame_buffer.h"
#include "render_thread_info.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include <dlfcn.h>
#include <memory>
#define PLB_GL(name) gfxstream::host::gl::s_gles2.name
#else
#define PLB_GL(name) name
#endif

namespace {
void require(bool okay, const char *message) {
    if (!okay)
        throw std::runtime_error(message);
}
bool property(const char *name, const char *expected) {
    char value[PROP_VALUE_MAX];
    __system_property_get(name, value);
    return !strcmp(value, expected);
}
bool extension(const char *list, const char *name) {
    if (!list)
        return false;
    for (const char *at = list; (at = strstr(at, name)); at++) {
        size_t length = strlen(name);
        if ((at == list || at[-1] == ' ') && (at[length] == ' ' || !at[length]))
            return true;
    }
    return false;
}
struct Graphics {
#ifdef PLB_YUV_TRANSLATOR
    bool initialized = false;
    uint32_t emulatedContext = 0, emulatedSurface = 0;
    std::unique_ptr<gfxstream::host::RenderThreadInfo> thread;
#endif
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLImageKHR image = EGL_NO_IMAGE_KHR;
    AHardwareBuffer *buffer = nullptr;
    GLuint texture = 0, framebuffer = 0;
    ~Graphics() {
        if (context != EGL_NO_CONTEXT) {
            PLB_GL(glFinish)();
            PLB_GL(glDeleteFramebuffers)(1, &framebuffer);
            PLB_GL(glDeleteTextures)(1, &texture);
        }
        if (image != EGL_NO_IMAGE_KHR) {
#ifdef PLB_YUV_TRANSLATOR
            auto destroy = gfxstream::host::gl::s_egl.eglDestroyImageKHR;
#else
            auto destroy = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
                eglGetProcAddress("eglDestroyImageKHR"));
#endif
            if (destroy)
                destroy(display, image);
        }
        if (buffer)
            AHardwareBuffer_release(buffer);
#ifdef PLB_YUV_TRANSLATOR
        auto fb = gfxstream::host::FrameBuffer::getFB();
        if (fb && thread) {
            fb->bindContext(0, 0, 0);
            if (emulatedSurface)
                fb->destroyEmulatedEglWindowSurface(emulatedSurface);
            if (emulatedContext)
                fb->destroyEmulatedEglContext(emulatedContext);
        }
        thread.reset();
        if (initialized)
            stream_renderer_teardown();
#else
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface != EGL_NO_SURFACE)
                eglDestroySurface(display, surface);
            if (context != EGL_NO_CONTEXT)
                eglDestroyContext(display, context);
            eglTerminate(display);
        }
#endif
    }
    void initialize() {
#ifdef PLB_YUV_TRANSLATOR
        // Offscreen translator only: no Activity, ANativeWindow, display endpoint
        // or VM. The ordinary extension whitelist remains unchanged.
        const char features[] = "Vulkan:disabled";
        static int cookie = 1;
        auto fence = +[](void *, stream_renderer_fence *) {};
        auto debug = +[](void *, stream_renderer_debug *message) {
            if (message && message->message)
                fprintf(stderr, "PLB_YUV_RENDERER_LOG %.600s\n", message->message);
        };
        stream_renderer_param parameters[] = {
            {STREAM_RENDERER_PARAM_USER_DATA, reinterpret_cast<uint64_t>(&cookie)},
            {STREAM_RENDERER_PARAM_FENCE_CALLBACK, reinterpret_cast<uint64_t>(fence)},
            {STREAM_RENDERER_PARAM_DEBUG_CALLBACK, reinterpret_cast<uint64_t>(debug)},
            {STREAM_RENDERER_PARAM_WIN0_WIDTH, 16},
            {STREAM_RENDERER_PARAM_WIN0_HEIGHT, 16},
            {STREAM_RENDERER_PARAM_RENDERER_FLAGS, STREAM_RENDERER_FLAGS_USE_EGL_BIT |
                                                       STREAM_RENDERER_FLAGS_USE_SURFACELESS_BIT |
                                                       STREAM_RENDERER_FLAGS_USE_GLES_BIT},
            {STREAM_RENDERER_PARAM_RENDERER_FEATURES, reinterpret_cast<uint64_t>(features)},
        };
        require(!stream_renderer_init(parameters, sizeof(parameters) / sizeof(*parameters)),
                "translator initialize");
        initialized = true;
        auto fb = gfxstream::host::FrameBuffer::getFB();
        require(fb, "translator framebuffer");
        thread = std::make_unique<gfxstream::host::RenderThreadInfo>();
        thread->initGl();
        EGLint attrs[] = {EGL_SURFACE_TYPE,
                          EGL_PBUFFER_BIT,
                          EGL_RENDERABLE_TYPE,
                          EGL_OPENGL_ES3_BIT,
                          EGL_RED_SIZE,
                          8,
                          EGL_GREEN_SIZE,
                          8,
                          EGL_BLUE_SIZE,
                          8,
                          EGL_NONE};
        EGLint config = -1;
        require(fb->chooseConfig(attrs, &config, 1) >= 1 && config >= 0, "translator config");
        emulatedContext = fb->createEmulatedEglContext(config, 0, gfxstream::host::gl::GLESApi_3_0);
        emulatedSurface = fb->createEmulatedEglWindowSurface(config, 16, 16);
        require(emulatedContext && emulatedSurface &&
                    fb->bindContext(emulatedContext, emulatedSurface, emulatedSurface),
                "translator context");
        auto &egl = gfxstream::host::gl::s_egl;
        display = egl.eglGetCurrentDisplay();
        context = egl.eglGetCurrentContext();
#else
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        require(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr), "EGL init");
        EGLint attributes[] = {EGL_SURFACE_TYPE,
                               EGL_PBUFFER_BIT,
                               EGL_RENDERABLE_TYPE,
                               EGL_OPENGL_ES3_BIT,
                               EGL_RED_SIZE,
                               8,
                               EGL_GREEN_SIZE,
                               8,
                               EGL_BLUE_SIZE,
                               8,
                               EGL_NONE};
        EGLConfig config;
        EGLint count = 0;
        require(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1,
                "EGL config");
        EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, size);
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
        require(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
                    eglMakeCurrent(display, surface, surface, context),
                "EGL context");
#endif
    }
    void createYuv() {
        AHardwareBuffer_Desc desc{};
        desc.width = desc.height = 16;
        desc.layers = 1;
        desc.format = AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
        desc.usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                     AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                     AHARDWAREBUFFER_USAGE_CPU_READ_RARELY;
        require(AHardwareBuffer_isSupported(&desc), "YUV hardware buffer unsupported");
        require(!AHardwareBuffer_allocate(&desc, &buffer), "YUV allocation");
#ifdef PLB_YUV_TRANSLATOR
        static void *hostEgl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
        auto client = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
            hostEgl ? dlsym(hostEgl, "eglGetNativeClientBufferANDROID") : nullptr);
        auto create = gfxstream::host::gl::s_egl.eglCreateImageKHR;
        auto target = gfxstream::host::gl::s_gles2.glEGLImageTargetTexture2DOES;
#else
        auto client = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
            eglGetProcAddress("eglGetNativeClientBufferANDROID"));
        auto create =
            reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
        auto target = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));
#endif
        require(client && create && target, "EGL image entry points");
        const EGLint preserved[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        image =
            create(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client(buffer), preserved);
        require(image != EGL_NO_IMAGE_KHR, "YUV EGL image");
#ifdef PLB_YUV_TRANSLATOR
        require(gfxstream::host::gl::s_egl.eglSetImageInfoANDROID(display, image, 16, 16, GL_RGB),
                "image dimensions");
#endif
        PLB_GL(glGenTextures)(1, &texture);
        PLB_GL(glBindTexture)(GL_TEXTURE_EXTERNAL_OES, texture);
        PLB_GL(glTexParameteri)(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        PLB_GL(glTexParameteri)(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        target(GL_TEXTURE_EXTERNAL_OES, image);
        PLB_GL(glGenFramebuffers)(1, &framebuffer);
        PLB_GL(glBindFramebuffer)(GL_FRAMEBUFFER, framebuffer);
        PLB_GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_EXTERNAL_OES, texture, 0);
        require(PLB_GL(glGetError)() == GL_NO_ERROR &&
                    PLB_GL(glCheckFramebufferStatus)(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "YUV framebuffer");
    }
};
GLuint shader(GLenum kind, const std::string &text) {
    GLuint result = PLB_GL(glCreateShader)(kind);
    const char *source = text.c_str();
    PLB_GL(glShaderSource)(result, 1, &source, nullptr);
    PLB_GL(glCompileShader)(result);
    GLint okay = 0;
    PLB_GL(glGetShaderiv)(result, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        char log[2048] = {};
        PLB_GL(glGetShaderInfoLog)(result, sizeof(log), nullptr, log);
        fprintf(stderr, "YUV_SHADER_LOG %s\n", log);
        PLB_GL(glDeleteShader)(result);
        throw std::runtime_error("YUV shader compile");
    }
    return result;
}
GLuint linkProgram(const std::string &fragment) {
    const char *vertex = "#version 300 es\n"
                         "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                         "gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
    GLuint vs = 0, fs = 0, linked = 0;
    try {
        vs = shader(GL_VERTEX_SHADER, vertex);
        fs = shader(GL_FRAGMENT_SHADER, fragment);
        linked = PLB_GL(glCreateProgram)();
        PLB_GL(glAttachShader)(linked, vs);
        PLB_GL(glAttachShader)(linked, fs);
        PLB_GL(glLinkProgram)(linked);
        GLint okay = 0;
        PLB_GL(glGetProgramiv)(linked, GL_LINK_STATUS, &okay);
        require(okay, "YUV program link");
    } catch (...) {
        PLB_GL(glDeleteProgram)(linked);
        PLB_GL(glDeleteShader)(vs);
        PLB_GL(glDeleteShader)(fs);
        throw;
    }
    PLB_GL(glDeleteShader)(vs);
    PLB_GL(glDeleteShader)(fs);
    return linked;
}
void verifyPlanes(AHardwareBuffer *buffer, const int expected[3]) {
    AHardwareBuffer_Planes planes{};
    require(!AHardwareBuffer_lockPlanes(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_RARELY, -1, nullptr,
                                        &planes),
            "YUV plane lock");
    size_t checked = 0, errors = 0;
    if (planes.planeCount == 3) {
        for (int plane = 0; plane < 3; plane++) {
            const auto &p = planes.planes[plane];
            int side = plane ? 8 : 16;
            if (!p.data || !p.pixelStride || p.rowStride < (unsigned)side * p.pixelStride) {
                errors++;
                continue;
            }
            auto data = static_cast<const unsigned char *>(p.data);
            for (int y = 0; y < side; y++)
                for (int x = 0; x < side; x++) {
                    int value = data[y * p.rowStride + x * p.pixelStride];
                    errors += abs(value - expected[plane]) > 3;
                    checked++;
                }
        }
    } else {
        errors++;
    }
    int unlocked = AHardwareBuffer_unlock(buffer, nullptr);
    printf("PLB_YUV_PLANES checked=%zu errors=%zu\n", checked, errors);
    require(!unlocked && checked == 384 && !errors, "YUV plane contents");
}
// Read the actual raw YUV sampler into an ordinary RGBA target. This catches
// a misleading RGB-backed implementation that clips out-of-gamut YUV values.
struct RgbaTarget {
    GLuint texture = 0, framebuffer = 0, program = 0;
    ~RgbaTarget() {
        PLB_GL(glDeleteProgram)(program);
        PLB_GL(glDeleteFramebuffers)(1, &framebuffer);
        PLB_GL(glDeleteTextures)(1, &texture);
    }
    void create() {
        PLB_GL(glGenTextures)(1, &texture);
        PLB_GL(glBindTexture)(GL_TEXTURE_2D, texture);
        PLB_GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             nullptr);
        PLB_GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        PLB_GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        PLB_GL(glGenFramebuffers)(1, &framebuffer);
        PLB_GL(glBindFramebuffer)(GL_FRAMEBUFFER, framebuffer);
        PLB_GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture,
                                       0);
        require(PLB_GL(glGetError)() == GL_NO_ERROR &&
                    PLB_GL(glCheckFramebufferStatus)(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "RGBA framebuffer regression");
    }
    void verify(const int expected[4], int tolerance, const char *label) {
        unsigned char pixels[16 * 16 * 4]{};
        PLB_GL(glReadPixels)(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        size_t errors = 0;
        for (int index = 0; index < 16 * 16 * 4; index++)
            errors += abs(pixels[index] - expected[index % 4]) > tolerance;
        printf("PLB_YUV_SAMPLE kind=%s checked=1024 errors=%zu\n", label, errors);
        require(!errors && PLB_GL(glGetError)() == GL_NO_ERROR, "YUV sampler contents");
    }
};
void verifySampling(Graphics &graphics) {
    RgbaTarget target;
    target.create();
    GLint twoD = 0, external = 0;
    PLB_GL(glGetIntegerv)(GL_TEXTURE_BINDING_2D, &twoD);
    PLB_GL(glGetIntegerv)(GL_TEXTURE_BINDING_EXTERNAL_OES, &external);
    require(twoD == static_cast<GLint>(target.texture) &&
                external == static_cast<GLint>(graphics.texture),
            "independent texture bindings");
#ifdef PLB_YUV_TRANSLATOR
    auto fb = gfxstream::host::FrameBuffer::getFB();
    require(fb->bindContext(0, 0, 0) &&
                fb->bindContext(graphics.emulatedContext, graphics.emulatedSurface,
                                graphics.emulatedSurface),
            "translator context restoration");
    PLB_GL(glGetIntegerv)(GL_TEXTURE_BINDING_EXTERNAL_OES, &external);
    require(external == static_cast<GLint>(graphics.texture), "external binding restoration");
#endif
    const struct {
        const char *expression;
        const char *label;
        int expected[4];
    } cases[] = {
        {"sampled", "raw-out-of-gamut-yuv", {128, 0, 0, 255}},
        {"vec4(yuv_2_rgb(sampled.xyz,itu_709),1)", "yuv-to-rgb", {0, 226, 0, 255}},
    };
    for (const auto &test : cases) {
        target.program =
            linkProgram(std::string("#version 300 es\n#extension GL_EXT_YUV_target : require\n") +
                        "precision highp float;uniform highp __samplerExternal2DY2YEXT image;"
                        "out vec4 color;void main(){vec4 sampled=texture(image,vec2(0.5));color=" +
                        test.expression + ";}");
        PLB_GL(glUseProgram)(target.program);
        GLint location = PLB_GL(glGetUniformLocation)(target.program, "image");
        require(location >= 0, "raw YUV sampler uniform");
        PLB_GL(glUniform1i)(location, 0);
        PLB_GL(glDrawArrays)(GL_TRIANGLES, 0, 3);
        PLB_GL(glFinish)();
        require(PLB_GL(glGetError)() == GL_NO_ERROR, "raw YUV GPU sample");
        target.verify(test.expected, 4, test.label);
        PLB_GL(glDeleteProgram)(target.program);
        target.program = 0;
    }
    PLB_GL(glClearColor)(0.1f, 0.2f, 0.3f, 1.0f);
    PLB_GL(glClear)(GL_COLOR_BUFFER_BIT);
    const int rgba[] = {26, 51, 77, 255};
    target.verify(rgba, 1, "rgba-regression");
}
} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (getuid() || !property("ro.build.version.sdk", "37"))
        return 20;
#ifdef PLB_YUV_GUEST
    if (!property("ro.product.device", "vsoc_arm64_only") ||
        !property("ro.build.version.incremental", "16373615"))
        return 21;
#else
    if (!property("ro.product.model", "SM-S948Q") ||
        !property("ro.build.version.incremental", "S948QOPU1BZI9") || !plb_host_memory_healthy())
        return 21;
    int maintenance = profile_maintenance_lock(LOCK_SH);
    int maintenanceError = errno;
    int owner = profile_lock();
    if (maintenance < 0 || owner < 0) {
        fprintf(stderr, "PLB_YUV_LEASE_FAILED maintenance=%d errno=%d owner=%d errno=%d uid=%u\n",
                maintenance, maintenanceError, owner, errno, geteuid());
        return 22;
    }
#endif
    try {
        Graphics graphics;
        graphics.initialize();
        printf("PLB_YUV_RENDERER %s\n", PLB_GL(glGetString)(GL_RENDERER));
        bool supported =
            extension(reinterpret_cast<const char *>(PLB_GL(glGetString)(GL_EXTENSIONS)),
                      "GL_EXT_YUV_target");
        printf("PLB_YUV_EXTENSION advertised=%d\n", supported);
#ifndef PLB_YUV_TRANSLATOR
        if (!supported)
            return 77;
#else
        // This deliberately probes functionality below the guest API boundary;
        // it is not permission to advertise an incomplete guest extension.
        require(!supported, "experimental extension must stay unadvertised");
        puts("PLB_YUV_TRANSLATOR_DIAGNOSTIC unadvertised=1");
#endif
        graphics.createYuv();
        const struct {
            const char *expression;
            int expected[3];
        } cases[] = {
            {"vec4(0.25,0.5,0.75,1.0)", {64, 128, 191}},
            {"vec4(rgb_2_yuv(vec3(1,0,0),itu_601),1)", {81, 90, 240}},
            {"vec4(rgb_2_yuv(vec3(1,0,0),itu_601_full_range),1)", {76, 85, 255}},
            {"vec4(rgb_2_yuv(vec3(1,0,0),itu_709),1)", {63, 102, 240}},
            {"vec4(0.5,0.0,0.0,1.0)", {128, 0, 0}},
        };
        PLB_GL(glViewport)(0, 0, 16, 16);
        PLB_GL(glDisable)(GL_DITHER);
        for (const auto &test : cases) {
            GLuint linked = linkProgram(
                std::string("#version 300 es\n#extension GL_EXT_YUV_target : require\n") +
                "precision highp float;layout(yuv) out vec4 color;void main(){color=" +
                test.expression + ";}");
            PLB_GL(glUseProgram)(linked);
            PLB_GL(glDrawArrays)(GL_TRIANGLES, 0, 3);
            PLB_GL(glFinish)();
            GLenum error = PLB_GL(glGetError)();
            PLB_GL(glDeleteProgram)(linked);
            require(error == GL_NO_ERROR, "YUV GPU draw");
            verifyPlanes(graphics.buffer, test.expected);
        }
        verifySampling(graphics);
        puts("PLB_YUV_REAL_RENDER_PASS");
        return 0;
    } catch (const std::exception &error) {
        // Graphics has already unwound here. Never query a destroyed or not-yet
        // initialized translator dispatch table while reporting another error.
        fprintf(stderr, "PLB_YUV_FAILED %s\n", error.what());
        return 30;
    }
}
