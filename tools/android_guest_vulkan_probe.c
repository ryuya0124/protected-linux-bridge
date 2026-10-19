/* MIT. Nonce-bound, guest-only Vulkan clear/readback on a real GPU device.
 * No window, host files or global settings. Two phases check every RGBA pixel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define CHECK(call)                                                                                \
    do {                                                                                           \
        VkResult result = (call);                                                                  \
        if (result != VK_SUCCESS) {                                                                \
            fprintf(stderr, "PLB_GUEST_VULKAN_FAILED call=%s result=%d\n", #call, result);         \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)

static int property(const char *name, const char *expected) {
    char value[PROP_VALUE_MAX];
    return __system_property_get(name, value) > 0 && !strcmp(value, expected);
}

static int memory_type(VkPhysicalDeviceMemoryProperties *memory, uint32_t bits,
                       VkMemoryPropertyFlags required) {
    for (uint32_t i = 0; i < memory->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (memory->memoryTypes[i].propertyFlags & required) == required)
            return (int)i;
    return -1;
}

int main(int argc, char **argv) {
    alarm(20);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getuid() || argc != 2 || strlen(argv[1]) != 32 ||
        strspn(argv[1], "0123456789abcdef") != 32 ||
        !property("ro.product.device", "vsoc_arm64_only") || !property("ro.hardware", "cutf_cvm") ||
        !property("ro.build.version.incremental", "16373615"))
        return 20;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE, buffer_memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    int passed = 0, submitted = 0;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "PLB guest Vulkan readback",
                             .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                   .pApplicationInfo = &app};
    CHECK(vkCreateInstance(&create, NULL, &instance));
    uint32_t count = 8;
    VkPhysicalDevice physicals[8];
    CHECK(vkEnumeratePhysicalDevices(instance, &count, physicals));
    if (count != 1)
        goto done;
    VkPhysicalDevice physical = physicals[0];
    VkPhysicalDeviceProperties actual;
    vkGetPhysicalDeviceProperties(physical, &actual);
    printf("PLB_GUEST_VULKAN_DEVICE nonce=%s name=%.255s type=%u api=%u vendor=%u\n", argv[1],
           actual.deviceName, actual.deviceType, actual.apiVersion, actual.vendorID);
    if (actual.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ||
        strstr(actual.deviceName, "SwiftShader") || strstr(actual.deviceName, "llvmpipe") ||
        strstr(actual.deviceName, "lavapipe"))
        goto done;
    VkQueueFamilyProperties families[64];
    count = 64;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    if (count > 64)
        goto done;
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        family++;
    if (family == count)
        goto done;
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                          .queueFamilyIndex = family,
                                          .queueCount = 1,
                                          .pQueuePriorities = &priority};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                      .queueCreateInfoCount = 1,
                                      .pQueueCreateInfos = &queue_info};
    CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                    .imageType = VK_IMAGE_TYPE_2D,
                                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                                    .extent = {64, 64, 1},
                                    .mipLevels = 1,
                                    .arrayLayers = 1,
                                    .samples = VK_SAMPLE_COUNT_1_BIT,
                                    .tiling = VK_IMAGE_TILING_OPTIMAL,
                                    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                             VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    CHECK(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    int index =
        memory_type(&memory, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (index < 0 || !requirements.size || requirements.size > 4 * 1024 * 1024)
        goto done;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                       .allocationSize = requirements.size,
                                       .memoryTypeIndex = (uint32_t)index};
    CHECK(vkAllocateMemory(device, &allocation, NULL, &image_memory));
    CHECK(vkBindImageMemory(device, image, image_memory, 0));
    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .size = 64 * 64 * 4,
                                      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    index = memory_type(&memory, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (index < 0 || !requirements.size || requirements.size > 4 * 1024 * 1024)
        goto done;
    int coherent =
        !!(memory.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = (uint32_t)index;
    CHECK(vkAllocateMemory(device, &allocation, NULL, &buffer_memory));
    CHECK(vkBindBufferMemory(device, buffer, buffer_memory, 0));
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                         .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                         .queueFamilyIndex = family};
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBufferAllocateInfo command_info = {.sType =
                                                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                .commandPool = pool,
                                                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                .commandBufferCount = 1};
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    const unsigned char expected[2][4] = {{64, 128, 191, 255}, {191, 64, 128, 255}};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (int phase = 0; phase < 2; phase++) {
        CHECK(vkResetCommandBuffer(command, 0));
        CHECK(vkResetFences(device, 1, &fence));
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        CHECK(vkBeginCommandBuffer(command, &begin));
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                        .srcAccessMask = phase ? VK_ACCESS_TRANSFER_READ_BIT : 0,
                                        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                        .oldLayout = phase ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                           : VK_IMAGE_LAYOUT_UNDEFINED,
                                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .image = image,
                                        .subresourceRange = range};
        vkCmdPipelineBarrier(
            command, phase ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        VkClearColorValue color;
        for (int channel = 0; channel < 4; channel++)
            color.float32[channel] = expected[phase][channel] / 255.0f;
        vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                             &range);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                  .imageExtent = {64, 64, 1}};
        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                               &copy);
        VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                      .buffer = buffer,
                                      .offset = 0,
                                      .size = VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                             0, NULL, 1, &host, 0, NULL);
        CHECK(vkEndCommandBuffer(command));
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                               .commandBufferCount = 1,
                               .pCommandBuffers = &command};
        CHECK(vkQueueSubmit(queue, 1, &submit, fence));
        submitted = 1;
        CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ULL));
        submitted = 0;
        unsigned char *pixels;
        CHECK(vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, (void **)&pixels));
        if (!coherent) {
            VkMappedMemoryRange invalidate = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                              .memory = buffer_memory,
                                              .size = VK_WHOLE_SIZE};
            VkResult result = vkInvalidateMappedMemoryRanges(device, 1, &invalidate);
            if (result) {
                vkUnmapMemory(device, buffer_memory);
                goto done;
            }
        }
        unsigned errors = 0;
        for (int i = 0; i < 64 * 64 * 4; i++)
            if (abs((int)pixels[i] - expected[phase][i % 4]) > 1)
                errors++;
        vkUnmapMemory(device, buffer_memory);
        printf("PLB_GUEST_VULKAN_READBACK nonce=%s phase=%d bytes=16384 errors=%u\n", argv[1],
               phase, errors);
        if (errors)
            goto done;
    }
    passed = 1;
done:
    if (submitted) {
        // Do not destroy resources still used by a timed-out submission.
        // Exit this guest diagnostic process; do not stop the VM or host.
        fflush(NULL);
        _exit(31);
    }
    if (device) {
        if (fence)
            vkDestroyFence(device, fence, NULL);
        if (pool)
            vkDestroyCommandPool(device, pool, NULL);
        if (buffer)
            vkDestroyBuffer(device, buffer, NULL);
        if (image)
            vkDestroyImage(device, image, NULL);
        if (buffer_memory)
            vkFreeMemory(device, buffer_memory, NULL);
        if (image_memory)
            vkFreeMemory(device, image_memory, NULL);
        vkDestroyDevice(device, NULL);
    }
    if (instance)
        vkDestroyInstance(instance, NULL);
    printf("PLB_GUEST_VULKAN_COMPLETE nonce=%s passed=%d\n", argv[1], passed);
    return passed ? 0 : 30;
}
