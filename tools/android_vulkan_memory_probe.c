/* MIT. Bounded host Vulkan allocation diagnostic; no VM or policy changes.
 * All images are 64x64. Never query an unbound AHB image's requirements: Vulkan
 * requires that image to be bound first. Allocation sizes are real driver data.
 */
#define VK_USE_PLATFORM_ANDROID_KHR
#include <android/hardware_buffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

static VkPhysicalDevice physical;
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static PFN_vkGetAndroidHardwareBufferPropertiesANDROID ahb_properties;
static PFN_vkGetMemoryAndroidHardwareBufferANDROID export_ahb;

static int memory_type(uint32_t bits) {
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
        if ((bits & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            return (int)i;
    return -1;
}

static int image_case(const char *name, VkExternalMemoryHandleTypeFlagBits handle,
                      int import_buffer) {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    AHardwareBuffer *buffer = NULL;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    int passed = 0;
    VkExternalMemoryImageCreateInfo external = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, .handleTypes = handle};
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .pNext = handle ? &external : NULL,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = VK_FORMAT_R8G8B8A8_UNORM,
                              .extent = {64, 64, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage =
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (handle) {
        VkPhysicalDeviceExternalImageFormatInfo external_query = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
            .handleType = handle};
        VkPhysicalDeviceImageFormatInfo2 query = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
            .pNext = &external_query,
            .format = info.format,
            .type = info.imageType,
            .tiling = info.tiling,
            .usage = info.usage};
        VkExternalImageFormatProperties external_properties = {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
        VkImageFormatProperties2 properties = {.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
                                               .pNext = &external_properties};
        result = vkGetPhysicalDeviceImageFormatProperties2(physical, &query, &properties);
        VkExternalMemoryFeatureFlags required = import_buffer
                                                    ? VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT
                                                    : VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT;
        printf("PLB_VK_FORMAT case=%s result=%d features=%u\n", name, result,
               external_properties.externalMemoryProperties.externalMemoryFeatures);
        if (result ||
            !(external_properties.externalMemoryProperties.externalMemoryFeatures & required))
            goto done;
    }
    VkAndroidHardwareBufferPropertiesANDROID properties = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    if (import_buffer) {
        AHardwareBuffer_Desc description = {.width = 64,
                                            .height = 64,
                                            .layers = 1,
                                            .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
                                            .usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                                                     AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
        int allocation = AHardwareBuffer_allocate(&description, &buffer);
        printf("PLB_VK_AHB_ALLOCATE result=%d\n", allocation);
        if (allocation || !buffer)
            goto done;
        result = ahb_properties(device, buffer, &properties);
        if (result || !properties.allocationSize || properties.allocationSize > 4 * 1024 * 1024)
            goto done;
    }
    result = vkCreateImage(device, &info, NULL, &image);
    printf("PLB_VK_IMAGE case=%s result=%d\n", name, result);
    if (result)
        goto done;
    int ahb = handle == VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkMemoryRequirements requirements = {0};
    int index;
    if (ahb) {
        /* Export uses allocationSize=0 plus the dedicated image, as specified.
         * Import uses the actual AHardwareBuffer's properties. No guessed size.
         */
        index = memory_type(import_buffer ? properties.memoryTypeBits : UINT32_MAX);
    } else {
        vkGetImageMemoryRequirements(device, image, &requirements);
        printf("PLB_VK_REQUIREMENTS case=%s size=%llu alignment=%llu bits=%u\n", name,
               (unsigned long long)requirements.size, (unsigned long long)requirements.alignment,
               requirements.memoryTypeBits);
        if (!requirements.size || requirements.size > 4 * 1024 * 1024)
            goto done;
        index = memory_type(requirements.memoryTypeBits);
    }
    if (index < 0)
        goto done;
    VkImportAndroidHardwareBufferInfoANDROID imported = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, .buffer = buffer};
    VkExportMemoryAllocateInfo exported = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                                           .handleTypes = handle};
    VkMemoryDedicatedAllocateInfo dedicated = {.sType =
                                                   VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                                               .pNext = import_buffer ? (void *)&imported
                                                        : handle      ? (void *)&exported
                                                                      : NULL,
                                               .image = image};
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                       .pNext = &dedicated,
                                       .allocationSize =
                                           ahb ? properties.allocationSize : requirements.size,
                                       .memoryTypeIndex = (uint32_t)index};
    result = vkAllocateMemory(device, &allocation, NULL, &memory);
    printf("PLB_VK_MEMORY case=%s result=%d requested=%llu type=%d dedicated=1\n", name, result,
           (unsigned long long)allocation.allocationSize, index);
    if (result)
        goto done;
    result = vkBindImageMemory(device, image, memory, 0);
    printf("PLB_VK_BIND case=%s result=%d\n", name, result);
    if (result)
        goto done;
    if (ahb) {
        if (!import_buffer) {
            VkMemoryGetAndroidHardwareBufferInfoANDROID get = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
                .memory = memory};
            result = export_ahb(device, &get, &buffer);
            if (result || !buffer)
                goto done;
            result = ahb_properties(device, buffer, &properties);
            if (result || !properties.allocationSize)
                goto done;
        }
        vkGetImageMemoryRequirements(device, image, &requirements);
        printf("PLB_VK_BOUND_AHB case=%s buffer_size=%llu image_size=%llu bits=%u\n", name,
               (unsigned long long)properties.allocationSize, (unsigned long long)requirements.size,
               properties.memoryTypeBits);
        if (!requirements.size)
            goto done;
    }
    passed = 1;
done:
    if (image)
        vkDestroyImage(device, image, NULL);
    if (memory)
        vkFreeMemory(device, memory, NULL);
    if (buffer)
        AHardwareBuffer_release(buffer);
    printf("PLB_VK_CASE case=%s passed=%d\n", name, passed);
    return passed;
}

int main(void) {
    alarm(15);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getuid())
        return 20;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "PLB Vulkan allocation diagnostic",
                             .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                   .pApplicationInfo = &app};
    VkInstance instance;
    if (vkCreateInstance(&create, NULL, &instance))
        return 21;
    uint32_t count = 8;
    VkPhysicalDevice devices[8];
    if (vkEnumeratePhysicalDevices(instance, &count, devices) || count != 1)
        return 22;
    physical = devices[0];
    VkPhysicalDeviceProperties actual;
    vkGetPhysicalDeviceProperties(physical, &actual);
    printf("PLB_VK_DEVICE name=%.255s api=%u driver=%u\n", actual.deviceName, actual.apiVersion,
           actual.driverVersion);
    if (actual.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        return 23;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    VkExtensionProperties extensions[256];
    count = 256;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, extensions))
        return 24;
    const char *enabled[4];
    uint32_t enabled_count = 0;
    int ahb = 0, fd = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!strcmp(extensions[i].extensionName,
                    VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME))
            ahb = 1;
        else if (!strcmp(extensions[i].extensionName, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))
            fd = 1;
        else
            continue;
        enabled[enabled_count++] = extensions[i].extensionName;
    }
    VkQueueFamilyProperties queues[64];
    count = 64;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues);
    if (count > 64)
        return 25;
    uint32_t family = 0;
    while (family < count && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        family++;
    if (family == count)
        return 26;
    float priority = 1;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                     .queueFamilyIndex = family,
                                     .queueCount = 1,
                                     .pQueuePriorities = &priority};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                      .queueCreateInfoCount = 1,
                                      .pQueueCreateInfos = &queue,
                                      .enabledExtensionCount = enabled_count,
                                      .ppEnabledExtensionNames = enabled};
    if (vkCreateDevice(physical, &device_info, NULL, &device))
        return 27;
    ahb_properties = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(
        device, "vkGetAndroidHardwareBufferPropertiesANDROID");
    export_ahb = (PFN_vkGetMemoryAndroidHardwareBufferANDROID)vkGetDeviceProcAddr(
        device, "vkGetMemoryAndroidHardwareBufferANDROID");
    int plain = image_case("ordinary", 0, 0);
    int opaque = fd ? image_case("opaque-fd", VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, 0) : 0;
    int exported = 0, imported = 0;
    if (ahb && ahb_properties && export_ahb) {
        exported = image_case(
            "ahb-export", VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID, 0);
        imported = image_case(
            "ahb-import", VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID, 1);
    }
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    printf("PLB_VK_MEMORY_COMPLETE ordinary=%d opaque_fd=%d ahb_export=%d ahb_import=%d\n", plain,
           opaque, exported, imported);
    return plain && exported && imported ? 0 : 28;
}
