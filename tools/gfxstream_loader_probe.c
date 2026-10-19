/* MIT. Read-only user-space loader diagnostic. No VM, policy or host-file edits.
 * Uses only exported symbols verified in the installed gfxstream library.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

int main(void) {
    alarm(10);
    if (getuid())
        return 20;
    void *loader = dlopen("/system/lib64/libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!loader) {
        printf("PLB_VULKAN_LOADER_FAILED %s\n", dlerror());
        return 21;
    }
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)dlsym(loader, "vkCreateInstance");
    PFN_vkGetInstanceProcAddr get =
        (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
    printf("PLB_VULKAN_ENTRYPOINTS create=%d get=%d\n", create != NULL, get != NULL);
    if (!create || !get)
        return 22;
    printf("PLB_VULKAN_GLOBAL_LOOKUP create=%d extensions=%d layers=%d\n",
           get(VK_NULL_HANDLE, "vkCreateInstance") != NULL,
           get(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties") != NULL,
           get(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties") != NULL);
    VkInstance instance;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "PLB loader diagnostic",
                             .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &app};
    VkResult result = create(&info, NULL, &instance);
    printf("PLB_VULKAN_INSTANCE result=%d\n", result);
    if (result != VK_SUCCESS)
        return 23;
    printf("PLB_VULKAN_INSTANCE_LOOKUP create=%d extensions=%d layers=%d\n",
           get(instance, "vkCreateInstance") != NULL,
           get(instance, "vkEnumerateInstanceExtensionProperties") != NULL,
           get(instance, "vkEnumerateInstanceLayerProperties") != NULL);
    PFN_vkEnumeratePhysicalDevices devices =
        (PFN_vkEnumeratePhysicalDevices)get(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties properties =
        (PFN_vkGetPhysicalDeviceProperties)get(instance, "vkGetPhysicalDeviceProperties");
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)get(instance, "vkDestroyInstance");
    if (!devices || !properties || !destroy)
        return 27;
    uint32_t count = 8;
    VkPhysicalDevice physical[8];
    result = devices(instance, &count, physical);
    printf("PLB_VULKAN_DEVICES result=%d count=%u\n", result, count);
    if (result == VK_SUCCESS)
        for (uint32_t i = 0; i < count; i++) {
            VkPhysicalDeviceProperties actual;
            properties(physical[i], &actual);
            printf("PLB_VULKAN_DEVICE type=%u name=%.255s api=%u\n", actual.deviceType,
                   actual.deviceName, actual.apiVersion);
        }
    destroy(instance, NULL);
    void *backend =
        dlopen("/apex/com.android.virt/lib64/libgfxstream_backend.so", RTLD_NOW | RTLD_LOCAL);
    if (!backend) {
        printf("PLB_GFXSTREAM_LOAD_FAILED %s\n", dlerror());
        return 24;
    }
    void *(*open_library)(const char *, char *, size_t) =
        dlsym(backend, "_ZN9gfxstream4base13SharedLibrary4openEPKcPcm");
    void *(*symbol)(void *, const char *) =
        dlsym(backend, "_ZNK9gfxstream4base13SharedLibrary10findSymbolEPKc");
    if (!open_library || !symbol)
        return 25;
    char error[1024] = {0};
    void *lib = open_library("/system/lib64/libvulkan.so", error, sizeof(error));
    printf("PLB_GFXSTREAM_LOADER opened=%d create=%d error=%.1023s\n", lib != NULL,
           lib && symbol(lib, "vkCreateInstance"), error);
    return lib && symbol(lib, "vkCreateInstance") ? 0 : 26;
}
