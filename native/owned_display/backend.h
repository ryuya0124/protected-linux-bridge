/* MIT. App-private Android 17 display ABI. No system service registration. */
#ifndef PLB_OWNED_DISPLAY_BACKEND_H
#define PLB_OWNED_DISPLAY_BACKEND_H
#include <android/binder_ibinder.h>
#include <android/native_window.h>
#include <cstddef>
#include <cstdint>

// Export only this C ABI. Keep the statically linked C++ runtime and generated
// Binder implementation private, so a VMM's own libc++ cannot interpose them.
#pragma GCC visibility push(default)
extern "C" {
typedef void (*ErrorCallback)(const char *);
typedef bool (*PlbDisplayPublication)(AIBinder *, bool published, void *cookie);
// Configure exactly once, before the private VMM's display initialization.
int plb_owned_display_configure(int viewer_uid, const char *session, PlbDisplayPublication publish,
                                void *cookie);
struct AndroidDisplayContext;
struct AndroidDisplaySurface;
struct AHardwareBufferInfo {
    size_t num_fds;
    const int32_t *fds_ptr;
    size_t metadata_len;
    const uint8_t *metadata_ptr;
};
AndroidDisplayContext *create_android_display_context(const char *, ErrorCallback);
void destroy_android_display_context(AndroidDisplayContext *);
AndroidDisplaySurface *create_android_surface(AndroidDisplayContext *, uint32_t, uint32_t, bool);
void destroy_android_surface(AndroidDisplayContext *, AndroidDisplaySurface *);
bool get_android_surface_buffer(AndroidDisplayContext *, AndroidDisplaySurface *,
                                ANativeWindow_Buffer *);
void post_android_surface_buffer(AndroidDisplayContext *, AndroidDisplaySurface *);
void set_android_surface_position(AndroidDisplayContext *, uint32_t, uint32_t);
void android_display_flip_to(AndroidDisplayContext *, AndroidDisplaySurface *,
                             const AHardwareBufferInfo *);
}
#pragma GCC visibility pop
#endif
