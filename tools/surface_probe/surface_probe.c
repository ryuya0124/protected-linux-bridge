/* MIT. Fixed three-second GPU clear into an app-owned, Binder-transferred Surface.
 * This is a transport prerequisite probe, not Android guest GPU presentation.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

JNIEXPORT jint JNICALL Java_net_ryuya_1dev_protectedlinux_surfaceprobe_RootSurfaceProbe_draw(
    JNIEnv *env, jclass type, jobject surface) {
    (void)type;
    if (getuid())
        return 20;
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    if (!window)
        return 21;
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface target = EGL_NO_SURFACE;
    int result = 22;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL))
        goto done;
    EGLint attributes[] = {EGL_SURFACE_TYPE,
                           EGL_WINDOW_BIT,
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
    EGLint count, format;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1 ||
        !eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format) ||
        ANativeWindow_setBuffersGeometry(window, 480, 800, format))
        goto done;
    EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    target = eglCreateWindowSurface(display, config, window, NULL);
    if (context == EGL_NO_CONTEXT || target == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, target, target, context))
        goto done;
    fprintf(stdout, "PLB_SURFACE_GPU renderer=%s\n", glGetString(GL_RENDERER));
    fflush(stdout);
    glViewport(0, 0, 480, 800);
    for (unsigned frame = 0; frame < 90; frame++) {
        glClearColor(frame < 30, frame >= 30 && frame < 60, frame >= 60, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (glGetError() != GL_NO_ERROR || !eglSwapBuffers(display, target))
            goto done;
        struct timespec delay = {.tv_nsec = 33333333};
        nanosleep(&delay, NULL);
    }
    puts("PLB_SURFACE_GPU_COMPLETE frames=90");
    fflush(stdout);
    result = 0;
done:
    if (result)
        fprintf(stderr, "PLB_SURFACE_GPU_FAILED egl=%x\n", eglGetError());
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (target != EGL_NO_SURFACE)
            eglDestroySurface(display, target);
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        eglTerminate(display);
    }
    ANativeWindow_release(window);
    return result;
}

/* FD-based loading retains the exact file that Java hashed, without a path race. */
#include <android/dlext.h>
#include <android/file_descriptor_jni.h>
#include <dlfcn.h>
#include <sys/stat.h>
typedef int (*plb_vmm_entry)(int, const char *const *, size_t);
static plb_vmm_entry load_vmm(JNIEnv *env, jobject descriptor) {
    if (getuid())
        return NULL;
    int fd = AFileDescriptor_getFd(env, descriptor);
    struct stat file;
    if (fd < 0 || fstat(fd, &file) || !S_ISREG(file.st_mode) || file.st_uid || file.st_nlink != 1 ||
        (file.st_mode & 0777) != 0500 || file.st_size < 1024 || file.st_size > 32 * 1024 * 1024)
        return NULL;
    android_dlextinfo options = {.flags = ANDROID_DLEXT_USE_LIBRARY_FD, .library_fd = fd};
#ifndef PLB_SURFACE_VMM_LIBRARY
#define PLB_SURFACE_VMM_LIBRARY                                                                    \
    "/data/user/0/net.ryuya_dev/protectedlinux/files/kernel-probes/libcrosvm-surface.so"
#endif
    void *library = android_dlopen_ext(PLB_SURFACE_VMM_LIBRARY, RTLD_NOW | RTLD_LOCAL, &options);
    if (!library) {
        printf("PLB_SURFACE_VMM_LOAD_FAILED %s\n", dlerror());
        return NULL;
    }
    return (plb_vmm_entry)dlsym(library, "plb_crosvm_surface_main");
}
JNIEXPORT jint JNICALL Java_net_ryuya_1dev_protectedlinux_surfaceprobe_RootVmmLoadProbe_check(
    JNIEnv *env, jclass type, jobject descriptor) {
    (void)type;
    plb_vmm_entry main = load_vmm(env, descriptor);
    if (!main)
        return 53;
    const char *const args[] = {"crosvm", "--help"};
    int result = main(2, args, 0);
    fflush(stdout);
    return result;
}

JNIEXPORT jint JNICALL Java_net_ryuya_1dev_protectedlinux_surfaceprobe_RootSurfaceVmm_run(
    JNIEnv *env, jclass type, jobject descriptor, jobject surface, jobjectArray arguments) {
    (void)type;
    if (getuid())
        return 50;
    jsize count = (*env)->GetArrayLength(env, arguments);
    if (count < 2 || count > 160)
        return 51;
    plb_vmm_entry main = load_vmm(env, descriptor);
    if (!main)
        return 53;
    ANativeWindow *window = ANativeWindow_fromSurface(env, surface);
    if (!window)
        return 54;
    jstring values[160] = {0};
    const char *argv[160] = {0};
    int result = 55;
    for (jsize i = 0; i < count; i++) {
        values[i] = (*env)->GetObjectArrayElement(env, arguments, i);
        if (!values[i] || (*env)->GetStringUTFLength(env, values[i]) > 4096)
            goto cleanup;
        argv[i] = (*env)->GetStringUTFChars(env, values[i], NULL);
        if (!argv[i])
            goto cleanup;
    }
    printf("PLB_SURFACE_VMM_BEGIN argc=%d\n", count);
    fflush(stdout);
    result = main(count, argv, (size_t)window);
cleanup:
    for (jsize i = 0; i < count; i++) {
        if (argv[i])
            (*env)->ReleaseStringUTFChars(env, values[i], argv[i]);
        if (values[i])
            (*env)->DeleteLocalRef(env, values[i]);
    }
    ANativeWindow_release(window);
    return result;
}
