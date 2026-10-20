/* MIT. Owned Binder + AHardwareBuffer display backend. No VM or memory donation code. */
#include "backend.h"
#include "gate.h"
#include <aidl/net/ryuya_dev/protectedlinux/display/BnOwnedDisplay.h>
#include <android/binder_ibinder_jni.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <android/surface_control.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <jni.h>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <unistd.h>
#include <vector>

namespace {
using aidl::android::hardware::NativeWindow;
using aidl::net::ryuya_dev::protectedlinux::display::BnOwnedDisplay;
struct Slot {
    ANativeWindow *window = nullptr;
    ASurfaceControl *control = nullptr;
    AHardwareBuffer *last = nullptr;
    uint32_t width = 0, height = 0;
    int32_t x = 0, y = 0;
    bool created = false;
    void remove() {
        if (control) {
            auto transaction = ASurfaceTransaction_create();
            if (transaction) {
                ASurfaceTransaction_setVisibility(transaction, control,
                                                  ASURFACE_TRANSACTION_VISIBILITY_HIDE);
                ASurfaceTransaction_apply(transaction);
                ASurfaceTransaction_delete(transaction);
            }
            ASurfaceControl_release(control);
        }
        if (window)
            ANativeWindow_release(window);
        control = nullptr;
        window = nullptr;
    }
    ~Slot() {
        remove();
        if (last)
            AHardwareBuffer_release(last);
    }
};
struct State {
    std::mutex mutex;
    plb::DisplayGate gate;
    bool active = true;
    std::array<Slot, 2> slots;
    explicit State(int uid, const std::string &session) : gate(uid, session) {
    }
    bool present(Slot &slot) {
        if (!slot.control || !slot.last)
            return true;
        ASurfaceTransaction *transaction = ASurfaceTransaction_create();
        if (!transaction)
            return false;
        ASurfaceTransaction_setBuffer(transaction, slot.control, slot.last, -1);
        ASurfaceTransaction_setPosition(transaction, slot.control, slot.x, slot.y);
        ASurfaceTransaction_setVisibility(transaction, slot.control,
                                          ASURFACE_TRANSACTION_VISIBILITY_SHOW);
        ASurfaceTransaction_apply(transaction);
        ASurfaceTransaction_delete(transaction);
        return true;
    }
    void close() {
        std::lock_guard lock(mutex);
        active = false;
        gate.close();
        for (auto &slot : slots) {
            slot.remove();
            if (slot.last)
                AHardwareBuffer_release(slot.last);
            slot.last = nullptr;
        }
    }
};
class Service final : public BnOwnedDisplay {
  public:
    Service(std::shared_ptr<State> state, int uid) : state_(std::move(state)), uid_(uid) {
    }
    ndk::ScopedAStatus getProtocolVersion(int32_t *result) override {
        if (AIBinder_getCallingUid() != static_cast<uid_t>(uid_))
            return denied();
        *result = 1;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus attach(const std::string &session, int64_t generation,
                              const NativeWindow &surface, bool cursor) override {
        if (!surface.get())
            return denied();
        std::lock_guard lock(state_->mutex);
        if (!state_->gate.attach(AIBinder_getCallingUid(), session, generation, cursor))
            return denied();
        auto &slot = state_->slots[cursor ? 1 : 0];
        ASurfaceControl *control =
            ASurfaceControl_createFromWindow(surface.get(), "PLB owned display");
        if (!control)
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        ANativeWindow_acquire(surface.get());
        slot.remove();
        slot.control = control;
        slot.window = surface.get();
        state_->present(slot);
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus detach(const std::string &session, int64_t generation,
                              bool cursor) override {
        std::lock_guard lock(state_->mutex);
        if (!state_->gate.detach(AIBinder_getCallingUid(), session, generation, cursor))
            return denied();
        state_->slots[cursor ? 1 : 0].remove();
        return ndk::ScopedAStatus::ok();
    }

  private:
    static ndk::ScopedAStatus denied() {
        return ndk::ScopedAStatus::fromExceptionCode(EX_SECURITY);
    }
    std::shared_ptr<State> state_;
    int uid_;
};
struct Configuration {
    std::mutex mutex;
    bool configured = false, consumed = false;
    pid_t pid = 0;
    int uid = -1;
    std::string session;
    PlbDisplayPublication publish = nullptr;
    void *cookie = nullptr;
} config;
using ImportAhb = int (*)(const AHardwareBuffer_Desc *, const void *, int32_t, AHardwareBuffer **);
AHardwareBuffer *import_buffer(const AHardwareBufferInfo *info) {
    if (!info || !info->fds_ptr || !info->metadata_ptr || info->num_fds == 0 ||
        info->num_fds > 64 || info->metadata_len < sizeof(AHardwareBuffer_Desc) ||
        info->metadata_len > 16384 ||
        (info->metadata_len - sizeof(AHardwareBuffer_Desc)) % sizeof(int))
        return nullptr;
    AHardwareBuffer_Desc desc;
    memcpy(&desc, info->metadata_ptr, sizeof(desc));
    if (!desc.width || !desc.height || desc.width > 16384 || desc.height > 16384 ||
        desc.layers != 1 || desc.rfu0 || desc.rfu1 ||
        (desc.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT))
        return nullptr;
    for (size_t i = 0; i < info->num_fds; i++)
        if (info->fds_ptr[i] < 0 || fcntl(info->fds_ptr[i], F_GETFD) < 0)
            return nullptr;
    const size_t ints = (info->metadata_len - sizeof(desc)) / sizeof(int);
    std::vector<int> handle(3 + info->num_fds + ints);
    handle[0] = 3 * sizeof(int);
    handle[1] = info->num_fds;
    handle[2] = ints;
    memcpy(handle.data() + 3, info->fds_ptr, info->num_fds * sizeof(int));
    memcpy(handle.data() + 3 + info->num_fds, info->metadata_ptr + sizeof(desc),
           ints * sizeof(int));
    // This VNDK entry is not in NDK link stubs. Require the real implementation;
    // never fabricate a handle or remove a protected-content flag to import it.
    static auto import =
        reinterpret_cast<ImportAhb>(dlsym(RTLD_DEFAULT, "AHardwareBuffer_createFromHandle"));
    AHardwareBuffer *result = nullptr;
    constexpr int clone_method = 3; // AOSP hardware_buffer.h CLONE, duplicated owned FDs.
    if (!import || import(&desc, handle.data(), clone_method, &result))
        return nullptr;
    return result;
}
} // namespace

struct AndroidDisplayContext {
    std::shared_ptr<State> state;
    std::shared_ptr<Service> service;
    PlbDisplayPublication publish;
    void *cookie;
    ErrorCallback error;
    std::array<AndroidDisplaySurface *, 2> surfaces{};
};
struct AndroidDisplaySurface {
    std::shared_ptr<State> state;
    bool cursor;
};
extern "C" int plb_owned_display_configure(int uid, const char *token,
                                           PlbDisplayPublication publish, void *cookie) {
    if (getuid() || uid < 10000 || uid > 19999 || !token || strnlen(token, 65) != 64 || !publish)
        return 1;
    std::string session(token);
    if (!plb::valid_session(session))
        return 1;
    std::lock_guard lock(config.mutex);
    if (config.configured)
        return 2;
    config.pid = getpid();
    config.uid = uid;
    config.session = session;
    config.publish = publish;
    config.cookie = cookie;
    config.configured = true;
    return 0;
}
extern "C" AndroidDisplayContext *create_android_display_context(const char *,
                                                                 ErrorCallback error) {
    std::lock_guard lock(config.mutex);
    if (getuid() || !config.configured || config.consumed || config.pid != getpid())
        return nullptr;
    config.consumed = true;
    auto state = std::make_shared<State>(config.uid, config.session);
    auto service = ndk::SharedRefBase::make<Service>(state, config.uid);
    using StartPool = void (*)();
    auto start = reinterpret_cast<StartPool>(dlsym(RTLD_DEFAULT, "ABinderProcess_startThreadPool"));
    // app_process may already have started its shared Binder pool. Never shrink
    // its configured maximum; startThreadPool itself is idempotent.
    if (!start)
        return nullptr;
    start();
    if (!config.publish(service->asBinder().get(), true, config.cookie)) {
        state->close();
        config.publish(service->asBinder().get(), false, config.cookie);
        return nullptr;
    }
    return new AndroidDisplayContext{state, service, config.publish, config.cookie, error};
}
extern "C" void destroy_android_display_context(AndroidDisplayContext *ctx) {
    if (!ctx)
        return;
    ctx->state->close();
    ctx->publish(ctx->service->asBinder().get(), false, ctx->cookie);
    for (auto *surface : ctx->surfaces)
        delete surface;
    delete ctx;
}
extern "C" AndroidDisplaySurface *create_android_surface(AndroidDisplayContext *ctx, uint32_t width,
                                                         uint32_t height, bool cursor) {
    if (!ctx || !width || !height || width > 16384 || height > 16384)
        return nullptr;
    std::lock_guard lock(ctx->state->mutex);
    auto &slot = ctx->state->slots[cursor ? 1 : 0];
    if (!ctx->state->active)
        return nullptr;
    slot.created = true;
    slot.width = width;
    slot.height = height;
    auto &surface = ctx->surfaces[cursor ? 1 : 0];
    if (!surface)
        surface = new AndroidDisplaySurface{ctx->state, cursor};
    return surface;
}
extern "C" void destroy_android_surface(AndroidDisplayContext *, AndroidDisplaySurface *) {
    // The pinned Rust backend currently does not call this hook. Fixed per-context
    // handles match its upstream service slots and remain valid until context death.
}
extern "C" bool get_android_surface_buffer(AndroidDisplayContext *, AndroidDisplaySurface *,
                                           ANativeWindow_Buffer *) {
    return false; // AHB-only transport: no silent full-frame CPU fallback.
}
extern "C" void post_android_surface_buffer(AndroidDisplayContext *, AndroidDisplaySurface *) {
}
extern "C" void set_android_surface_position(AndroidDisplayContext *ctx, uint32_t x, uint32_t y) {
    if (!ctx || x > INT32_MAX || y > INT32_MAX)
        return;
    std::lock_guard lock(ctx->state->mutex);
    auto &slot = ctx->state->slots[1];
    slot.x = x;
    slot.y = y;
    ctx->state->present(slot);
}
extern "C" void android_display_flip_to(AndroidDisplayContext *ctx, AndroidDisplaySurface *surface,
                                        const AHardwareBufferInfo *info) {
    if (!ctx || !surface || ctx->state != surface->state)
        return;
    AHardwareBuffer *buffer = import_buffer(info);
    if (!buffer) {
        if (ctx->error)
            ctx->error("PLB owned display: AHB import refused");
        return;
    }
    std::lock_guard lock(ctx->state->mutex);
    auto &slot = ctx->state->slots[surface->cursor ? 1 : 0];
    if (!ctx->state->active || !slot.created) {
        AHardwareBuffer_release(buffer);
        return;
    }
    if (slot.last)
        AHardwareBuffer_release(slot.last);
    slot.last = buffer;
    ctx->state->present(slot);
}

namespace {
JavaVM *java_vm = nullptr;
jclass java_publisher = nullptr;
jmethodID java_exchange = nullptr;
bool java_publication(AIBinder *binder, bool published, void *) {
    JNIEnv *env = nullptr;
    bool attached = java_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK;
    if (attached && java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
        return false;
    jobject remote = AIBinder_toJavaBinder(env, binder);
    bool result =
        remote && env->CallStaticBooleanMethod(java_publisher, java_exchange, remote, published);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = false;
    }
    if (remote)
        env->DeleteLocalRef(remote);
    if (attached)
        java_vm->DetachCurrentThread();
    return result;
}
} // namespace
extern "C" JNIEXPORT jint JNICALL
Java_net_ryuya_1dev_protectedlinux_displayroot_RootOwnedDisplayPublisher_configure(JNIEnv *env,
                                                                                   jclass type,
                                                                                   jint uid,
                                                                                   jstring token) {
    if (getuid() || java_vm || !token)
        return 1;
    if (env->GetJavaVM(&java_vm) != JNI_OK)
        return 1;
    java_publisher = static_cast<jclass>(env->NewGlobalRef(type));
    java_exchange = env->GetStaticMethodID(type, "exchange", "(Landroid/os/IBinder;Z)Z");
    if (!java_publisher || !java_exchange)
        return 1;
    const char *text = env->GetStringUTFChars(token, nullptr);
    if (!text)
        return 1;
    int result = plb_owned_display_configure(uid, text, java_publication, nullptr);
    env->ReleaseStringUTFChars(token, text);
    return result;
}
