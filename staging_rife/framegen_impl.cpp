#include "framegen_impl.hpp"
#include "rife_net.hpp"

#include <volk.h>

#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#include <vulkan/vulkan_android.h>
#endif

#include <android/log.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "mat.h"

#define LOG_TAG "rife-framegen"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace rife_framegen {
namespace {

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

struct AhbImageRes {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

struct StagingBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
    bool coherent = false;
};

struct Context {
    VkExtent2D extent{};
    uint32_t bpp = 0;
    AhbImageRes in0{};
    AhbImageRes in1{};
    std::vector<AhbImageRes> outs;
    StagingBuffer inStage{};
    StagingBuffer outStage{};
};

struct State {
    bool initialized = false;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::map<int32_t, Context> contexts;
    int32_t nextId = 1;
    bool isHdr = false;
    uint64_t genCount = 2;
    std::unique_ptr<RifeNet> net;
};

State g;
std::mutex gMutex;

// Mirrors LSFG::vulkan_error's message format so callers grepping for
// "error -4" (device lost) keep working.
[[noreturn]] void throwVk(VkResult res, const char* what)
{
    throw std::runtime_error(std::string(what) + " (error " + std::to_string(static_cast<int>(res)) + ")");
}

uint32_t bytesPerPixel(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
        return 4;
    default:
        throw std::runtime_error("unsupported AHB pixel format for RIFE framegen");
    }
}

// ---------------------------------------------------------------------------
// resource teardown
// ---------------------------------------------------------------------------

void destroyStaging(StagingBuffer& sb)
{
    if (sb.mapped)
        vkUnmapMemory(g.device, sb.memory);
    if (sb.buffer)
        vkDestroyBuffer(g.device, sb.buffer, nullptr);
    if (sb.memory)
        vkFreeMemory(g.device, sb.memory, nullptr);
    sb = StagingBuffer{};
}

void destroyAhbImage(AhbImageRes& res)
{
    if (res.image)
        vkDestroyImage(g.device, res.image, nullptr);
    if (res.memory)
        vkFreeMemory(g.device, res.memory, nullptr);
    res = AhbImageRes{};
}

void destroyContextResources(Context& ctx)
{
    destroyStaging(ctx.inStage);
    destroyStaging(ctx.outStage);
    destroyAhbImage(ctx.in0);
    destroyAhbImage(ctx.in1);
    for (auto& out : ctx.outs)
        destroyAhbImage(out);
    ctx.outs.clear();
}

void finalizeLocked()
{
    for (auto& kv : g.contexts)
        destroyContextResources(kv.second);
    g.contexts.clear();
    g.net.reset();
    if (g.cmd)
    {
        vkFreeCommandBuffers(g.device, g.cmdPool, 1, &g.cmd);
        g.cmd = VK_NULL_HANDLE;
    }
    if (g.fence)
    {
        vkDestroyFence(g.device, g.fence, nullptr);
        g.fence = VK_NULL_HANDLE;
    }
    if (g.cmdPool)
    {
        vkDestroyCommandPool(g.device, g.cmdPool, nullptr);
        g.cmdPool = VK_NULL_HANDLE;
    }
    if (g.device)
    {
        vkDestroyDevice(g.device, nullptr);
        g.device = VK_NULL_HANDLE;
    }
    if (g.instance)
    {
        vkDestroyInstance(g.instance, nullptr);
        g.instance = VK_NULL_HANDLE;
    }
    g.queue = VK_NULL_HANDLE;
    g.physicalDevice = VK_NULL_HANDLE;
    g.queueFamily = UINT32_MAX;
    g.initialized = false;
    g.nextId = 1;
}

// ---------------------------------------------------------------------------
// command submission (fully synchronous)
// ---------------------------------------------------------------------------

template <typename Record>
void submitAndWait(Record record)
{
    VkResult res = vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    if (res != VK_SUCCESS)
        throwVk(res, "Unable to wait for fence");
    res = vkResetFences(g.device, 1, &g.fence);
    if (res != VK_SUCCESS)
        throwVk(res, "Unable to reset fence");
    res = vkResetCommandBuffer(g.cmd, 0);
    if (res != VK_SUCCESS)
        throwVk(res, "Unable to reset command buffer");

    const VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr,
    };
    res = vkBeginCommandBuffer(g.cmd, &beginInfo);
    if (res != VK_SUCCESS)
        throwVk(res, "Unable to begin command buffer");
    record(g.cmd);
    res = vkEndCommandBuffer(g.cmd);
    if (res != VK_SUCCESS)
        throwVk(res, "Unable to end command buffer");

    const VkSubmitInfo submitInfo{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0, nullptr, nullptr,
        1, &g.cmd,
        0, nullptr,
    };
    res = vkQueueSubmit(g.queue, 1, &submitInfo, g.fence);
    if (res != VK_SUCCESS)
        throwVk(res, "Unable to submit command buffer");
    res = vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    if (res != VK_SUCCESS)
        throwVk(res, "Unable to wait for fence");
}

void copyImageToBuffer(VkImage image, VkBuffer buffer, uint32_t w, uint32_t h)
{
    submitAndWait([=](VkCommandBuffer cmd) {
        const VkBufferImageCopy region{
            0,                       // bufferOffset
            0,                       // bufferRowLength (tightly packed)
            0,                       // bufferImageHeight
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0},
            {w, h, 1},
        };
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &region);
    });
}

void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h)
{
    submitAndWait([=](VkCommandBuffer cmd) {
        const VkBufferImageCopy region{
            0,                       // bufferOffset
            0,                       // bufferRowLength (tightly packed)
            0,                       // bufferImageHeight
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0},
            {w, h, 1},
        };
        vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    });
}

// ---------------------------------------------------------------------------
// AHB import + staging allocation
// ---------------------------------------------------------------------------

#ifdef __ANDROID__

AhbImageRes importAhb(AHardwareBuffer* ahb, VkExtent2D extent, VkFormat requested)
{
    VkAndroidHardwareBufferFormatPropertiesANDROID fmtProps{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
    };
    VkAndroidHardwareBufferPropertiesANDROID ahbProps{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
        &fmtProps,
    };
    VkResult res = vkGetAndroidHardwareBufferPropertiesANDROID(g.device, ahb, &ahbProps);
    if (res != VK_SUCCESS)
        throwVk(res, "vkGetAndroidHardwareBufferPropertiesANDROID failed");

    if (fmtProps.format != requested)
    {
        LOGW("AHB format (%d) differs from requested format (%d) — using the AHB's",
             static_cast<int>(fmtProps.format), static_cast<int>(requested));
    }

    const VkExternalMemoryImageCreateInfo externalInfo{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        nullptr,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
    };
    const VkImageCreateInfo desc{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        &externalInfo,
        0,
        VK_IMAGE_TYPE_2D,
        fmtProps.format,
        {extent.width, extent.height, 1},
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image = VK_NULL_HANDLE;
    res = vkCreateImage(g.device, &desc, nullptr, &image);
    if (res != VK_SUCCESS || image == VK_NULL_HANDLE)
        throwVk(res, "Failed to create Vulkan image (AHB)");

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(g.physicalDevice, &memProps);
    uint32_t memType = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if (ahbProps.memoryTypeBits & (1u << i))
        {
            memType = i;
            break;
        }
    }
    if (memType == UINT32_MAX)
    {
        vkDestroyImage(g.device, image, nullptr);
        throw std::runtime_error("No memory type for AHB import");
    }

    const VkMemoryDedicatedAllocateInfo dedicated{
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        nullptr,
        image,
        VK_NULL_HANDLE,
    };
    const VkImportAndroidHardwareBufferInfoANDROID importInfo{
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
        &dedicated,
        ahb,
    };
    const VkMemoryAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        &importInfo,
        ahbProps.allocationSize,
        memType,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    res = vkAllocateMemory(g.device, &allocInfo, nullptr, &memory);
    if (res != VK_SUCCESS || memory == VK_NULL_HANDLE)
    {
        vkDestroyImage(g.device, image, nullptr);
        throwVk(res, "Failed to import AHB into VkDeviceMemory");
    }

    res = vkBindImageMemory(g.device, image, memory, 0);
    if (res != VK_SUCCESS)
    {
        vkFreeMemory(g.device, memory, nullptr);
        vkDestroyImage(g.device, image, nullptr);
        throwVk(res, "Failed to bind AHB memory");
    }

    return AhbImageRes{image, memory, fmtProps.format};
}

#endif // __ANDROID__

StagingBuffer createStaging(VkDeviceSize size)
{
    const VkBufferCreateInfo desc{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, nullptr,
    };
    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult res = vkCreateBuffer(g.device, &desc, nullptr, &buffer);
    if (res != VK_SUCCESS)
        throwVk(res, "Failed to create staging buffer");

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(g.device, buffer, &reqs);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(g.physicalDevice, &memProps);
    uint32_t memType = UINT32_MAX;
    bool coherent = false;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if (!(reqs.memoryTypeBits & (1u << i)))
            continue;
        const VkMemoryPropertyFlags flags = memProps.memoryTypes[i].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
            continue;
        if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        {
            memType = i;
            coherent = true;
            break;
        }
        if (memType == UINT32_MAX)
            memType = i; // HOST_CACHED fallback
    }
    if (memType == UINT32_MAX)
    {
        vkDestroyBuffer(g.device, buffer, nullptr);
        throw std::runtime_error("No host-visible memory type for staging buffer");
    }

    const VkMemoryAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        reqs.size,
        memType,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    res = vkAllocateMemory(g.device, &allocInfo, nullptr, &memory);
    if (res != VK_SUCCESS || memory == VK_NULL_HANDLE)
    {
        vkDestroyBuffer(g.device, buffer, nullptr);
        throwVk(res, "Failed to allocate staging memory");
    }
    res = vkBindBufferMemory(g.device, buffer, memory, 0);
    if (res != VK_SUCCESS)
    {
        vkFreeMemory(g.device, memory, nullptr);
        vkDestroyBuffer(g.device, buffer, nullptr);
        throwVk(res, "Failed to bind staging memory");
    }

    void* mapped = nullptr;
    res = vkMapMemory(g.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (res != VK_SUCCESS || mapped == nullptr)
    {
        vkFreeMemory(g.device, memory, nullptr);
        vkDestroyBuffer(g.device, buffer, nullptr);
        throwVk(res, "Failed to map staging memory");
    }

    return StagingBuffer{buffer, memory, mapped, size, coherent};
}

void invalidateStaging(const StagingBuffer& sb)
{
    if (sb.coherent)
        return;
    const VkMappedMemoryRange range{
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        nullptr,
        sb.memory,
        0,
        VK_WHOLE_SIZE,
    };
    vkInvalidateMappedMemoryRanges(g.device, 1, &range);
}

void flushStaging(const StagingBuffer& sb)
{
    if (sb.coherent)
        return;
    const VkMappedMemoryRange range{
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        nullptr,
        sb.memory,
        0,
        VK_WHOLE_SIZE,
    };
    vkFlushMappedMemoryRanges(g.device, 1, &range);
}

// ---------------------------------------------------------------------------
// staging <-> Mat conversion
// ---------------------------------------------------------------------------

ncnn::Mat stagingToMat(const StagingBuffer& sb, VkFormat format, uint32_t w, uint32_t h)
{
    invalidateStaging(sb);

    ncnn::Mat mat(static_cast<int>(w), static_cast<int>(h), 3);
    if (mat.empty())
        throw std::runtime_error("failed to allocate input Mat");

    if (format == VK_FORMAT_R16G16B16A16_SFLOAT)
    {
        const uint16_t* src = static_cast<const uint16_t*>(sb.mapped);
        for (uint32_t y = 0; y < h; ++y)
        {
            float* r = mat.channel(0).row(static_cast<int>(y));
            float* g = mat.channel(1).row(static_cast<int>(y));
            float* b = mat.channel(2).row(static_cast<int>(y));
            const uint16_t* row = src + (size_t)y * w * 4;
            for (uint32_t x = 0; x < w; ++x)
            {
                r[x] = ncnn::float16_to_float32(row[x * 4 + 0]);
                g[x] = ncnn::float16_to_float32(row[x * 4 + 1]);
                b[x] = ncnn::float16_to_float32(row[x * 4 + 2]);
            }
        }
    }
    else if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB
        || format == VK_FORMAT_B8G8R8A8_UNORM)
    {
        const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM;
        const uint8_t* src = static_cast<const uint8_t*>(sb.mapped);
        const float kScale = 1.0f / 255.0f;
        for (uint32_t y = 0; y < h; ++y)
        {
            float* r = mat.channel(bgra ? 2 : 0).row(static_cast<int>(y));
            float* g = mat.channel(1).row(static_cast<int>(y));
            float* b = mat.channel(bgra ? 0 : 2).row(static_cast<int>(y));
            const uint8_t* row = src + (size_t)y * w * 4;
            for (uint32_t x = 0; x < w; ++x)
            {
                r[x] = row[x * 4 + 0] * kScale;
                g[x] = row[x * 4 + 1] * kScale;
                b[x] = row[x * 4 + 2] * kScale;
            }
        }
    }
    else
    {
        throw std::runtime_error("unsupported pixel format for RIFE readback");
    }
    return mat;
}

void matToStaging(const ncnn::Mat& mat, StagingBuffer& sb, VkFormat format, uint32_t w, uint32_t h)
{
    if (format == VK_FORMAT_R16G16B16A16_SFLOAT)
    {
        uint16_t* dst = static_cast<uint16_t*>(sb.mapped);
        for (uint32_t y = 0; y < h; ++y)
        {
            const float* r = mat.channel(0).row(static_cast<int>(y));
            const float* g = mat.channel(1).row(static_cast<int>(y));
            const float* b = mat.channel(2).row(static_cast<int>(y));
            uint16_t* row = dst + (size_t)y * w * 4;
            for (uint32_t x = 0; x < w; ++x)
            {
                row[x * 4 + 0] = ncnn::float32_to_float16(r[x]);
                row[x * 4 + 1] = ncnn::float32_to_float16(g[x]);
                row[x * 4 + 2] = ncnn::float32_to_float16(b[x]);
                row[x * 4 + 3] = 0x3C00; // 1.0
            }
        }
    }
    else if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB
        || format == VK_FORMAT_B8G8R8A8_UNORM)
    {
        const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM;
        uint8_t* dst = static_cast<uint8_t*>(sb.mapped);
        for (uint32_t y = 0; y < h; ++y)
        {
            const float* r = mat.channel(0).row(static_cast<int>(y));
            const float* g = mat.channel(1).row(static_cast<int>(y));
            const float* b = mat.channel(2).row(static_cast<int>(y));
            uint8_t* row = dst + (size_t)y * w * 4;
            for (uint32_t x = 0; x < w; ++x)
            {
                row[x * 4 + (bgra ? 2 : 0)] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, r[x] * 255.0f + 0.5f)));
                row[x * 4 + 1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, g[x] * 255.0f + 0.5f)));
                row[x * 4 + (bgra ? 0 : 2)] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, b[x] * 255.0f + 0.5f)));
                row[x * 4 + 3] = 255;
            }
        }
    }
    else
    {
        throw std::runtime_error("unsupported pixel format for RIFE upload");
    }
    flushStaging(sb);
}

} // namespace

// ---------------------------------------------------------------------------
// public entry points
// ---------------------------------------------------------------------------

void initialize(uint64_t deviceUUID, bool isHdr, float flowScale, uint64_t generationCount,
    const Loader& loader)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (g.initialized)
    {
        LOGW("initialize called while already initialized — resetting previous state");
        finalizeLocked();
    }

    if (volkInitialize() != VK_SUCCESS)
        throw std::runtime_error("volkInitialize failed (no Vulkan loader?)");

    const char* instanceExtensions[] = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#ifdef __ANDROID__
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
#endif
    };
    const VkApplicationInfo appInfo{
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "rife-framegen",
        0,
        "rife-framegen",
        0,
        VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instanceInfo{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &appInfo,
        0, nullptr,
        static_cast<uint32_t>(sizeof(instanceExtensions) / sizeof(instanceExtensions[0])),
        instanceExtensions,
    };
    VkResult res = vkCreateInstance(&instanceInfo, nullptr, &g.instance);
    if (res != VK_SUCCESS)
        throwVk(res, "vkCreateInstance failed");
    volkLoadInstance(g.instance);

    // get physical device by uuid (vendorID << 32 | deviceID)
    uint32_t deviceCount = 0;
    res = vkEnumeratePhysicalDevices(g.instance, &deviceCount, nullptr);
    if (res != VK_SUCCESS || deviceCount == 0)
    {
        finalizeLocked();
        throwVk(res, "Failed to enumerate physical devices");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    res = vkEnumeratePhysicalDevices(g.instance, &deviceCount, devices.data());
    if (res != VK_SUCCESS)
    {
        finalizeLocked();
        throwVk(res, "Failed to get physical devices");
    }
    for (const auto& device : devices)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        const uint64_t uuid =
            static_cast<uint64_t>(properties.vendorID) << 32 | properties.deviceID;
        if (deviceUUID == uuid || deviceUUID == 0x1463ABAC)
        {
            g.physicalDevice = device;
            break;
        }
    }
    if (g.physicalDevice == VK_NULL_HANDLE)
    {
        finalizeLocked();
        throw std::runtime_error("Could not find physical device with UUID");
    }

    // find a compute-capable queue family
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(g.physicalDevice, &familyCount, families.data());
    for (uint32_t i = 0; i < familyCount; ++i)
    {
        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            g.queueFamily = i;
            break;
        }
    }
    if (g.queueFamily == UINT32_MAX)
    {
        finalizeLocked();
        throw std::runtime_error("No compute-capable queue family");
    }

    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,
        0,
        g.queueFamily,
        1,
        &priority,
    };
    const char* deviceExtensions[] = {
#ifdef __ANDROID__
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
#endif
    };
    const VkDeviceCreateInfo deviceInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,
        0,
        1, &queueInfo,
        0, nullptr,
        static_cast<uint32_t>(sizeof(deviceExtensions) / sizeof(deviceExtensions[0])),
        deviceExtensions,
        nullptr,
    };
    res = vkCreateDevice(g.physicalDevice, &deviceInfo, nullptr, &g.device);
    if (res != VK_SUCCESS)
    {
        finalizeLocked();
        throwVk(res, "vkCreateDevice failed");
    }
    volkLoadDevice(g.device);
    vkGetDeviceQueue(g.device, g.queueFamily, 0, &g.queue);

    const VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        g.queueFamily,
    };
    res = vkCreateCommandPool(g.device, &poolInfo, nullptr, &g.cmdPool);
    if (res != VK_SUCCESS)
    {
        finalizeLocked();
        throwVk(res, "vkCreateCommandPool failed");
    }
    const VkCommandBufferAllocateInfo cmdInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        g.cmdPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1,
    };
    res = vkAllocateCommandBuffers(g.device, &cmdInfo, &g.cmd);
    if (res != VK_SUCCESS)
    {
        finalizeLocked();
        throwVk(res, "vkAllocateCommandBuffers failed");
    }
    const VkFenceCreateInfo fenceInfo{
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        nullptr,
        VK_FENCE_CREATE_SIGNALED_BIT,
    };
    res = vkCreateFence(g.device, &fenceInfo, nullptr, &g.fence);
    if (res != VK_SUCCESS)
    {
        finalizeLocked();
        throwVk(res, "vkCreateFence failed");
    }

    // RIFE network: prefer fp16, fall back to fp32.
    g.net = std::make_unique<RifeNet>();
    if (!g.net->load(loader, true))
    {
        LOGW("RIFE model load with fp16 failed — retrying without fp16");
        g.net = std::make_unique<RifeNet>();
        if (!g.net->load(loader, false))
        {
            finalizeLocked();
            throw std::runtime_error("Failed to load RIFE flownet model");
        }
    }

    g.isHdr = isHdr;
    g.genCount = generationCount;
    g.initialized = true;
    LOGI("initialized: hdr=%d genCount=%llu flowScale=%.3f (v1 runs full-resolution inference; flowScale is ignored)",
         isHdr ? 1 : 0, static_cast<unsigned long long>(generationCount), flowScale);
}

#ifdef __ANDROID__

int32_t createContextFromAHB(AHardwareBuffer* in0, AHardwareBuffer* in1,
    const std::vector<AHardwareBuffer*>& outN, VkExtent2D extent, VkFormat format)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (!g.initialized)
        throw std::runtime_error("framegen not initialized");
    if (in0 == nullptr || in1 == nullptr)
        throw std::runtime_error("AHB is null");

    Context ctx;
    try
    {
        ctx.extent = extent;
        ctx.in0 = importAhb(in0, extent, format);
        ctx.in1 = importAhb(in1, extent, format);
        for (AHardwareBuffer* ahb : outN)
            ctx.outs.push_back(importAhb(ahb, extent, format));

        ctx.bpp = bytesPerPixel(ctx.in0.format);
        const VkDeviceSize stageSize =
            static_cast<VkDeviceSize>(extent.width) * extent.height * ctx.bpp;
        ctx.inStage = createStaging(stageSize);
        ctx.outStage = createStaging(stageSize);

        // Transition every imported image UNDEFINED -> GENERAL once; the Android
        // side hands AHB-backed images back and forth in GENERAL layout.
        submitAndWait([&ctx](VkCommandBuffer cmd) {
            const auto barrier = [cmd](VkImage image) {
                const VkImageMemoryBarrier b{
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    nullptr,
                    VK_ACCESS_NONE,
                    VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_QUEUE_FAMILY_IGNORED,
                    VK_QUEUE_FAMILY_IGNORED,
                    image,
                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            };
            barrier(ctx.in0.image);
            barrier(ctx.in1.image);
            for (const auto& out : ctx.outs)
                barrier(out.image);
        });
    }
    catch (...)
    {
        // free everything imported/allocated so far before propagating
        destroyContextResources(ctx);
        throw;
    }

    const int32_t id = g.nextId++;
    const size_t outCount = ctx.outs.size();
    g.contexts.emplace(id, std::move(ctx));
    LOGI("context %d created: %ux%u fmt=%d (%u bytes/px) outputs=%zu",
         id, extent.width, extent.height, static_cast<int>(format), ctx.bpp, outCount);
    return id;
}

#endif // __ANDROID__

void presentContext(int32_t id, int inSem, const std::vector<int>& outSem)
{
    // v1 is fully synchronous: no semaphores are consumed.
    (void)inSem;
    (void)outSem;

    std::lock_guard<std::mutex> lock(gMutex);
    try
    {
        auto it = g.contexts.find(id);
        if (it == g.contexts.end())
            throw std::runtime_error("unknown framegen context id");
        Context& ctx = it->second;
        if (!g.net)
            throw std::runtime_error("framegen has no RIFE network");

        const uint32_t w = ctx.extent.width;
        const uint32_t h = ctx.extent.height;

        // 1. read both inputs back to host memory
        copyImageToBuffer(ctx.in0.image, ctx.inStage.buffer, w, h);
        ncnn::Mat mat0 = stagingToMat(ctx.inStage, ctx.in0.format, w, h);
        copyImageToBuffer(ctx.in1.image, ctx.inStage.buffer, w, h);
        ncnn::Mat mat1 = stagingToMat(ctx.inStage, ctx.in1.format, w, h);

        // 2. interpolate and upload each generated frame
        const size_t n = ctx.outs.size();
        ncnn::Mat outMat;
        for (size_t k = 1; k <= n; ++k)
        {
            const float t = static_cast<float>(k) / static_cast<float>(n + 1);
            if (!g.net->run(mat0, mat1, t, outMat))
                throw std::runtime_error("RIFE inference failed");
            matToStaging(outMat, ctx.outStage, ctx.outs[k - 1].format, w, h);
            copyBufferToImage(ctx.outStage.buffer, ctx.outs[k - 1].image, w, h);
        }
    }
    catch (const std::exception& e)
    {
        LOGE("presentContext failed: %s", e.what());
        throw;
    }
}

void deleteContext(int32_t id)
{
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = g.contexts.find(id);
    if (it == g.contexts.end())
    {
        LOGW("deleteContext: unknown id %d", id);
        return;
    }
    destroyContextResources(it->second);
    g.contexts.erase(it);
    LOGI("context %d deleted", id);
}

void waitIdle()
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (g.device)
        vkDeviceWaitIdle(g.device);
}

void finalize()
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (!g.initialized && g.instance == VK_NULL_HANDLE)
        return;
    finalizeLocked();
    LOGI("finalized");
}

} // namespace rife_framegen
