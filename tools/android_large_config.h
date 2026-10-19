/* MIT. Shared build-time identity for separately stored Android RAM profiles.
 * Both variants retain the same host pins, health gate and policy guardian. */
#ifndef PLB_ANDROID_LARGE_CONFIG_H
#define PLB_ANDROID_LARGE_CONFIG_H
#ifndef PLB_ANDROID_LARGE_MEMORY_MIB
#define PLB_ANDROID_LARGE_MEMORY_MIB 1536
#endif
#define PLB_LARGE_STRING_INNER(value) #value
#define PLB_LARGE_STRING(value) PLB_LARGE_STRING_INNER(value)
#if PLB_ANDROID_LARGE_MEMORY_MIB == 1536
#define PLB_LARGE_DIRECTORY "cf17-1536"
#define PLB_LARGE_LIBRARY_NAME "libandroid-memory.so"
#define PLB_LARGE_READY_MARKER "plb-android-1536-v1\n"
#elif PLB_ANDROID_LARGE_MEMORY_MIB == 1792
#define PLB_LARGE_DIRECTORY "cf17-1792"
#define PLB_LARGE_LIBRARY_NAME "libandroid-memory1792.so"
#define PLB_LARGE_READY_MARKER "plb-android-1792-v1\n"
#else
#error "Only the separately prepared 1536/1792 MiB Android app profiles are supported"
#endif
#define PLB_LARGE_MEMORY_ARGUMENT "size=" PLB_LARGE_STRING(PLB_ANDROID_LARGE_MEMORY_MIB)
#define PLB_LARGE_BACKING_BYTES ((PLB_ANDROID_LARGE_MEMORY_MIB - 32UL) * 1024 * 1024)
#define PLB_LARGE_HEADROOM_KIB ((PLB_ANDROID_LARGE_MEMORY_MIB + 768UL) * 1024)
#endif
