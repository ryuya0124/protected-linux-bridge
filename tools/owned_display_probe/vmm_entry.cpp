/* MIT. Same-process JNI entry; no stock ELF patch, native-window setter or flush bypass. */
#include <dlfcn.h>
#include <cstdio>
#include <jni.h>
#include <string>
#include <vector>

/* Compile-time pinned variant only. No caller-selected library path. */
#ifndef PLB_OWNED_VMM_LIBRARY
#define PLB_OWNED_VMM_LIBRARY                                                                      \
    "/data/user/0/net.ryuya_dev.protectedlinux/files/android17-owned-runtime/"                     \
    "libplb_vmm_android17.so"
#endif

extern "C" JNIEXPORT jint JNICALL Java_net_ryuya_1dev_protectedlinux_displayroot_RootOwnedVmm_run(
    JNIEnv *env, jclass, jobjectArray arguments) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!arguments)
        return 70;
    int count = env->GetArrayLength(arguments);
    if (count < 10 || count > 160)
        return 71;
    std::vector<std::string> strings;
    for (int i = 0; i < count; i++) {
        auto text = static_cast<jstring>(env->GetObjectArrayElement(arguments, i));
        if (!text)
            return 72;
        const char *chars = env->GetStringUTFChars(text, nullptr);
        if (!chars)
            return 73;
        strings.emplace_back(chars);
        env->ReleaseStringUTFChars(text, chars);
        env->DeleteLocalRef(text);
        if (strings.back().size() > 4096)
            return 74;
    }
    std::vector<const char *> argv;
    for (const auto &string : strings)
        argv.push_back(string.c_str());
    void *library = dlopen(PLB_OWNED_VMM_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    // Keep the path literal in sync with the pinned private runtime, not caller input.
    if (!library)
        return 75;
    using Entry = int (*)(int, const char *const *);
    auto entry = reinterpret_cast<Entry>(dlsym(library, "plb_crosvm_owned_display_main"));
    if (!entry)
        return 76;
    return entry(count, argv.data());
}
