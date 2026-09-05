#define VMA_IMPLEMENTATION // The vk_mem_alloc.h included in VkContext.h will compile the bodies of the VMA functions (STB-style implementation)
#include "VkContext.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "Program.h"
#include "Texture.h"
#include "graphics.h"
#include "ImageFormat.h"
#include "shaderStructs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

namespace nebula {

// Singleton containing the Vulkan context.
static GraphicsContext gCtx;
GraphicsContext& ctx() {
    return gCtx;
}

void vkCheckImpl(VkResult result, const char* call, const char* file, int line) {
    if(result == VK_SUCCESS) {
        return;
    }
    char msg[512];
    std::snprintf(msg, sizeof(msg), "Vulkan call failed: %s (VkResult %d)", call, int(result));
    fatal(msg, file, line);
}

#ifdef NEBULA_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*
) {
    if(severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return VK_FALSE;
    }

    const bool isError = (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    (isError ? std::cerr : std::cout) << (isError ? "[VK][ERROR] " : "[VK] ")
                                       << data->pMessage << std::endl;

    if(isError) {
        breakInDebugger();
    }
    return VK_FALSE;
}

static bool hasInstanceLayer(const char* name) {
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for(const VkLayerProperties& layer : layers) {
        if(std::strcmp(layer.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

static VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo() {
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };
}
#endif

static bool hasDeviceExtension(VkPhysicalDevice physical, const char* name) {
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, exts.data());
    for(const VkExtensionProperties& ext : exts) {
        if(std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool findGraphicsPresentQueue(VkPhysicalDevice physical, VkSurfaceKHR surface, u32* outFamily) {
    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());

    for(u32 i = 0; i != count; ++i) {
        if(!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            continue;
        }
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present);
        if(present) {
            *outFamily = i;
            return true;
        }
    }
    return false;
}

static int rateDevice(VkPhysicalDevice physical, VkSurfaceKHR surface, u32* outFamily) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    if(props.apiVersion < VK_API_VERSION_1_3) {
        return 0;
    }

    VkPhysicalDeviceVulkan13Features vk13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    };
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk13,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features);

    if(!vk13.dynamicRendering || !vk13.synchronization2 || !features.features.samplerAnisotropy) {
        return 0;
    }

    if(!hasDeviceExtension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return 0;
    }
    if(!hasDeviceExtension(physical, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
        return 0;
    }

    if(!findGraphicsPresentQueue(physical, surface, outFamily)) {
        return 0;
    }

    int score = 1;
    if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    } else if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 100;
    }
    return score;
}

static void destroySwapchain() {
    for(VkSemaphore sem : gCtx.swapchainRenderSemaphores) {
        vkDestroySemaphore(gCtx.device, sem, nullptr);
    }
    gCtx.swapchainRenderSemaphores.clear();

    for(VkImageView view : gCtx.swapchainViews) {
        vkDestroyImageView(gCtx.device, view, nullptr);
    }
    gCtx.swapchainViews.clear();
    gCtx.swapchainImages.clear();

    if(gCtx.swapchain) {
        vkDestroySwapchainKHR(gCtx.device, gCtx.swapchain, nullptr);
        gCtx.swapchain = VK_NULL_HANDLE;
    }
}

static void destroyFrames() {
    for(InFlightFrame& frame : gCtx.frames) {
        if(frame.commandPool) {
            vkDestroyCommandPool(gCtx.device, frame.commandPool, nullptr);
        }
        if(frame.submitted) {
            vkDestroyFence(gCtx.device, frame.submitted, nullptr);
        }
        if(frame.acquire) {
            vkDestroySemaphore(gCtx.device, frame.acquire, nullptr);
        }
        frame = {};
    }
}

static VkSurfaceFormatKHR pickSurfaceFormat(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    u32 count = 0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()));

    for(const VkSurfaceFormatKHR& format : formats) {
        if(format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    ALWAYS_ASSERT(count, "No swapchain formats available");
    return formats[0];
}

static VkExtent2D pickSwapchainExtent(const VkSurfaceCapabilitiesKHR& caps) {
    // Wayland (and some other platforms) reports currentExtent as 0xFFFFFFFF:
    // the window size is not known to the driver, so we must pass GLFW's framebuffer size.
    if(caps.currentExtent.width != 0xFFFFFFFFu) {
        return caps.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(gCtx.window, &width, &height);

    VkExtent2D extent{u32(width), u32(height)};
    extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, extent.width));
    extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, extent.height));
    return extent;
}

static void createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gCtx.physicalDevice, gCtx.surface, &caps));

    const VkSurfaceFormatKHR surfaceFormat = pickSurfaceFormat(gCtx.physicalDevice, gCtx.surface);
    const VkExtent2D extent = pickSwapchainExtent(caps);
    ALWAYS_ASSERT(extent.width && extent.height, "Swapchain extent is zero");

    u32 imageCount = caps.minImageCount + 1;
    if(caps.maxImageCount && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    const VkSwapchainCreateInfoKHR swapchainCi{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = gCtx.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    vkCheck(vkCreateSwapchainKHR(gCtx.device, &swapchainCi, nullptr, &gCtx.swapchain));

    gCtx.swapchainFormat = surfaceFormat.format;
    gCtx.swapchainExtent = extent;
    gCtx.renderingColorFormat = surfaceFormat.format;

    u32 actualCount = 0;
    vkCheck(vkGetSwapchainImagesKHR(gCtx.device, gCtx.swapchain, &actualCount, nullptr));
    gCtx.swapchainImages.resize(actualCount);
    vkCheck(vkGetSwapchainImagesKHR(gCtx.device, gCtx.swapchain, &actualCount, gCtx.swapchainImages.data()));

    gCtx.swapchainViews.resize(actualCount);
    for(u32 i = 0; i != actualCount; ++i) {
        const VkImageViewCreateInfo viewCi{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = gCtx.swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = gCtx.swapchainFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        vkCheck(vkCreateImageView(gCtx.device, &viewCi, nullptr, &gCtx.swapchainViews[i]));
    }

    gCtx.swapchainRenderSemaphores.resize(actualCount);
    const VkSemaphoreCreateInfo semCi{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for(u32 i = 0; i != actualCount; ++i) {
        vkCheck(vkCreateSemaphore(gCtx.device, &semCi, nullptr, &gCtx.swapchainRenderSemaphores[i]));
    }
}

void waitForGpuIdle() {
    if(gCtx.device) {
        vkDeviceWaitIdle(gCtx.device);
    }
}

static void recreateSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(gCtx.window, &width, &height);
    if(width == 0 || height == 0) {
        return;
    }

    waitForGpuIdle();
    destroySwapchain();
    createSwapchain();
}

static void createFrames() {
    for(InFlightFrame& frame : gCtx.frames) {
        const VkCommandPoolCreateInfo poolCi{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = gCtx.graphicsQueueFamily,
        };
        vkCheck(vkCreateCommandPool(gCtx.device, &poolCi, nullptr, &frame.commandPool));

        const VkCommandBufferAllocateInfo allocCi{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkCheck(vkAllocateCommandBuffers(gCtx.device, &allocCi, &frame.commandBuffer));

        const VkFenceCreateInfo fenceCi{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        vkCheck(vkCreateFence(gCtx.device, &fenceCi, nullptr, &frame.submitted));

        const VkSemaphoreCreateInfo semCi{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        vkCheck(vkCreateSemaphore(gCtx.device, &semCi, nullptr, &frame.acquire));
    }
}

static void createTimestampPools() {
    u32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gCtx.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gCtx.physicalDevice, &familyCount, families.data());
    gCtx.timestampValidBits = families[gCtx.graphicsQueueFamily].timestampValidBits;

    if(!gCtx.timestampValidBits) {
        return;
    }

    for(u32 i = 0; i != framesInFlight; ++i) {
        const VkQueryPoolCreateInfo poolCi{
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = timestampQueriesPerFrame,
        };
        vkCheck(vkCreateQueryPool(gCtx.device, &poolCi, nullptr, &gCtx.timestampPools[i]));
    }
}

static void destroyTimestampPools() {
    for(u32 i = 0; i != framesInFlight; ++i) {
        if(gCtx.timestampPools[i]) {
            vkDestroyQueryPool(gCtx.device, gCtx.timestampPools[i], nullptr);
            gCtx.timestampPools[i] = VK_NULL_HANDLE;
        }
    }
}

static void createPipelineLayout() {
    const VkDescriptorSetLayoutBinding frameBindings[] = {
        {frameUboBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {frameLightsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {frameEnvBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {frameBrdfBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
    };
    const VkDescriptorSetLayoutCreateInfo frameSetCi{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = frameBindingCount,
        .pBindings = frameBindings,
    };
    vkCheck(vkCreateDescriptorSetLayout(gCtx.device, &frameSetCi, nullptr, &gCtx.frameSetLayout));

    const VkDescriptorSetLayoutBinding passBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {passStorageBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
    };
    const VkDescriptorSetLayoutCreateInfo passSetCi{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
        .bindingCount = passBindingCount,
        .pBindings = passBindings,
    };
    vkCheck(vkCreateDescriptorSetLayout(gCtx.device, &passSetCi, nullptr, &gCtx.passSetLayout));

    const VkDescriptorSetLayout setLayouts[] = {
        gCtx.frameSetLayout,
        gCtx.passSetLayout,
    };
    const VkPushConstantRange push{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    const VkPipelineLayoutCreateInfo layoutCi{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = setLayouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    vkCheck(vkCreatePipelineLayout(gCtx.device, &layoutCi, nullptr, &gCtx.pipelineLayout));
}

static void createSamplers() {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gCtx.physicalDevice, &props);

    const auto makeSampler = [&](VkSamplerAddressMode addressMode) {
        const VkSamplerCreateInfo ci{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = addressMode,
            .addressModeV = addressMode,
            .addressModeW = addressMode,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = props.limits.maxSamplerAnisotropy,
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE,
        };
        VkSampler sampler = VK_NULL_HANDLE;
        vkCheck(vkCreateSampler(gCtx.device, &ci, nullptr, &sampler));
        return sampler;
    };

    gCtx.samplerRepeat = makeSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT);
    gCtx.samplerClamp = makeSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

static void createFallbackSampledTexture() {
    const VkImageCreateInfo imageCi{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {1, 1, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocCi{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    vkCheck(vmaCreateImage(gCtx.allocator, &imageCi, &allocCi, &gCtx.fallbackSampledImage, &gCtx.fallbackSampledAllocation, nullptr));

    const VkImageViewCreateInfo viewCi{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = gCtx.fallbackSampledImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCheck(vkCreateImageView(gCtx.device, &viewCi, nullptr, &gCtx.fallbackSampledView));

    const u8 black[4] = {0, 0, 0, 255};

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    {
        const VkBufferCreateInfo bufferCi{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(black),
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo stagingAlloc{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo info{};
        vkCheck(vmaCreateBuffer(gCtx.allocator, &bufferCi, &stagingAlloc, &stagingBuffer, &stagingAllocation, &info));
        std::memcpy(info.pMappedData, black, sizeof(black));
    }

    immediateSubmit([&](VkCommandBuffer cmd) {
        imageBarrier(
            cmd,
            gCtx.fallbackSampledImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        const VkBufferImageCopy copy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {1, 1, 1},
        };
        vkCmdCopyBufferToImage(cmd, stagingBuffer, gCtx.fallbackSampledImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        imageBarrier(
            cmd,
            gCtx.fallbackSampledImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
    });

    vmaDestroyBuffer(gCtx.allocator, stagingBuffer, stagingAllocation);
}

static void createDescriptorPools() {
    const VkDescriptorPoolSize framePoolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, framesInFlight + 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight + 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (framesInFlight + 1) * 2},
    };
    const VkDescriptorPoolCreateInfo framePoolCi{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = framesInFlight + 1,
        .poolSizeCount = u32(std::size(framePoolSizes)),
        .pPoolSizes = framePoolSizes,
    };
    vkCheck(vkCreateDescriptorPool(gCtx.device, &framePoolCi, nullptr, &gCtx.frameDescriptorPool));

    VkDescriptorSetLayout frameLayouts[framesInFlight + 1];
    for(u32 i = 0; i != framesInFlight + 1; ++i) {
        frameLayouts[i] = gCtx.frameSetLayout;
    }
    const VkDescriptorSetAllocateInfo frameAlloc{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = gCtx.frameDescriptorPool,
        .descriptorSetCount = framesInFlight + 1,
        .pSetLayouts = frameLayouts,
    };
    VkDescriptorSet frameSets[framesInFlight + 1] = {};
    vkCheck(vkAllocateDescriptorSets(gCtx.device, &frameAlloc, frameSets));
    for(u32 i = 0; i != framesInFlight; ++i) {
        gCtx.frames[i].frameDescriptorSet = frameSets[i];
    }
}

static VkSampler samplerForTexture(const Texture* texture) {
    if(!texture) {
        return gCtx.samplerRepeat;
    }
    return texture->wrapMode() == WrapMode::Clamp ? gCtx.samplerClamp : gCtx.samplerRepeat;
}

// If the texture was never transitioned, report SHADER_READ_ONLY so the descriptor write stays valid.
static VkImageLayout sampledLayoutForTexture(const Texture* texture) {
    if(!texture || texture->vkLayout() == VK_IMAGE_LAYOUT_UNDEFINED) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return texture->vkLayout();
}

// PBR fallbacks when a texture slot is unbound (white albedo/emissive, flat normal, dielectric-rough).
static const Texture* defaultTextureForSlot(u32 slot) {
    switch(slot) {
        case 0:
        case 3:
            return defaultWhiteTexture().get();
        case 1:
            return defaultNormalTexture().get();
        case 2:
            return defaultMetalRoughTexture().get();
        default:
            return defaultBlackTexture().get();
    }
}

static VkDescriptorImageInfo sampledImageInfo(const Texture* texture, u32 defaultSlot) {
    if(!texture) {
        texture = defaultTextureForSlot(defaultSlot);
    }

    VkImageView view = gCtx.fallbackSampledView;
    if(texture && texture->vkView()) {
        view = texture->vkView();
    }

    return {
        .sampler = samplerForTexture(texture),
        .imageView = view,
        .imageLayout = sampledLayoutForTexture(texture),
    };
}

// Writes FrameResources into the persistent set for this slot and binds set 0.
void bindFrame(const FrameResources& frame) {
    if(!gCtx.pipelineLayout || !gCtx.frameSetLayout) {
        return;
    }

    const VkDescriptorSet set = gCtx.frames[gCtx.frameIndex].frameDescriptorSet;
    if(!set) {
        return;
    }

    ALWAYS_ASSERT(frame.ubo, "frame UBO is missing");
    ALWAYS_ASSERT(frame.lights, "frame lights are missing");

    const VkDescriptorBufferInfo uboInfo{
        .buffer = frame.ubo,
        .offset = 0,
        .range = frame.uboSize,
    };
    const VkDescriptorBufferInfo lightsInfo{
        .buffer = frame.lights,
        .offset = 0,
        .range = frame.lightsSize,
    };

    const VkDescriptorImageInfo envInfo = sampledImageInfo(frame.env, 4);
    const VkDescriptorImageInfo brdfInfo = sampledImageInfo(frame.brdf, 5);

    const VkWriteDescriptorSet writes[] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = frameUboBinding,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &uboInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = frameLightsBinding,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &lightsInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = frameEnvBinding,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &envInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = frameBrdfBinding,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &brdfInfo,
        },
    };
    vkUpdateDescriptorSets(gCtx.device, u32(std::size(writes)), writes, 0, nullptr);

    if(!vkIsRecording()) {
        return;
    }

    vkCmdBindDescriptorSets(
        vkCommandBuffer(),
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        gCtx.pipelineLayout,
        frameSet,
        1,
        &set,
        0,
        nullptr
    );
}

// Push PassResources into set 1 for the next draw/dispatch.
// Alternative approach: one persistent set per Material + sort draws by material.
void pushPassDescriptors(const PassResources& pass, bool compute) {
    if(!vkIsRecording() || !gCtx.pipelineLayout || !gCtx.passSetLayout) {
        return;
    }

    VkWriteDescriptorSet writes[passBindingCount] = {};
    u32 writeCount = 0;

    VkDescriptorImageInfo sampledInfos[passTextureSlotCount] = {};
    for(u32 slot = 0; slot != passTextureSlotCount; ++slot) {
        sampledInfos[slot] = sampledImageInfo(pass.textures[slot], slot);
        writes[writeCount++] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = slot,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &sampledInfos[slot],
        };
    }

    VkDescriptorImageInfo storageInfo{};
    if(pass.storageImage && pass.storageImage->vkStorageView()) {
        storageInfo = {
            .imageView = pass.storageImage->vkStorageView(),
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    } else {
        storageInfo = {
            .imageView = gCtx.fallbackSampledView,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    }
    writes[writeCount++] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding = passStorageBinding,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &storageInfo,
    };

    vkCmdPushDescriptorSetKHR(
        vkCommandBuffer(),
        compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
        gCtx.pipelineLayout,
        passSet,
        writeCount,
        writes
    );
}

static void createImmediatePool() {
    const VkCommandPoolCreateInfo poolCi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = gCtx.graphicsQueueFamily,
    };
    vkCheck(vkCreateCommandPool(gCtx.device, &poolCi, nullptr, &gCtx.immediatePool));
}

static u32 pendingDeletionSlot() {
    // During a frame, objects may still be referenced by this slot's command buffer.
    // Between frames, the last submitted slot is the one that may still be on the GPU.
    if(gCtx.frameActive) {
        return gCtx.frameIndex;
    }
    return (gCtx.frameIndex + framesInFlight - 1) % framesInFlight;
}

static void destroyEntry(const DeletionEntry& entry) {
    switch(entry.type) {
        case DeletionEntry::Type::Buffer:
            vmaDestroyBuffer(gCtx.allocator, entry.buffer, entry.allocation);
            break;
        case DeletionEntry::Type::Image:
            vmaDestroyImage(gCtx.allocator, entry.image, entry.allocation);
            break;
        case DeletionEntry::Type::ImageView:
            vkDestroyImageView(gCtx.device, entry.imageView, nullptr);
            break;
        case DeletionEntry::Type::Sampler:
            vkDestroySampler(gCtx.device, entry.sampler, nullptr);
            break;
        case DeletionEntry::Type::Pipeline:
            vkDestroyPipeline(gCtx.device, entry.pipeline, nullptr);
            break;
        case DeletionEntry::Type::ShaderModule:
            vkDestroyShaderModule(gCtx.device, entry.shaderModule, nullptr);
            break;
    }
}

void flushFrameDeletions(u32 frameSlot) {
    std::vector<DeletionEntry>& queue = gCtx.deletions[frameSlot];
    for(auto it = queue.rbegin(); it != queue.rend(); ++it) {
        destroyEntry(*it);
    }
    queue.clear();
}

void flushAllDeletions() {
    for(u32 i = 0; i != framesInFlight; ++i) {
        flushFrameDeletions(i);
    }
}

static void enqueueDeletion(DeletionEntry entry) {
    if(!gCtx.device) {
        return;
    }
    gCtx.deletions[pendingDeletionSlot()].push_back(entry);
}

void deferDestroy(VkBuffer buffer, VmaAllocation allocation) {
    if(!buffer) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::Buffer;
    entry.allocation = allocation;
    entry.buffer = buffer;
    enqueueDeletion(entry);
}

void deferDestroy(VkImage image, VmaAllocation allocation) {
    if(!image) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::Image;
    entry.allocation = allocation;
    entry.image = image;
    enqueueDeletion(entry);
}

void deferDestroy(VkImageView view) {
    if(!view) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::ImageView;
    entry.imageView = view;
    enqueueDeletion(entry);
}

void deferDestroy(VkSampler sampler) {
    if(!sampler) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::Sampler;
    entry.sampler = sampler;
    enqueueDeletion(entry);
}

void deferDestroy(VkPipeline pipeline) {
    if(!pipeline) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::Pipeline;
    entry.pipeline = pipeline;
    enqueueDeletion(entry);
}

void deferDestroy(VkShaderModule module) {
    if(!module) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::ShaderModule;
    entry.shaderModule = module;
    enqueueDeletion(entry);
}

void immediateSubmit(std::function<void(VkCommandBuffer)>&& record) {
    ALWAYS_ASSERT(gCtx.immediatePool && gCtx.graphicsQueue, "immediateSubmit called before vkInit");

    const VkCommandBufferAllocateInfo allocCi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = gCtx.immediatePool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkCheck(vkAllocateCommandBuffers(gCtx.device, &allocCi, &cmd));

    const VkCommandBufferBeginInfo beginCi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkCheck(vkBeginCommandBuffer(cmd, &beginCi));

    // Install as the current command buffer so bindGraphics / dispatch
    // (the GL-style API) record here instead of into a frame that is not active.
    const VkCommandBuffer prevImmediate = gCtx.immediateCmd;
    gCtx.immediateCmd = cmd;
    record(cmd);
    gCtx.immediateCmd = prevImmediate;

    vkCheck(vkEndCommandBuffer(cmd));

    const VkFenceCreateInfo fenceCi{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence = VK_NULL_HANDLE;
    vkCheck(vkCreateFence(gCtx.device, &fenceCi, nullptr, &fence));

    const VkCommandBufferSubmitInfo cmdInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
    };
    vkCheck(vkQueueSubmit2(gCtx.graphicsQueue, 1, &submit, fence));
    vkCheck(vkWaitForFences(gCtx.device, 1, &fence, VK_TRUE, UINT64_MAX));

    vkDestroyFence(gCtx.device, fence, nullptr);
    vkFreeCommandBuffers(gCtx.device, gCtx.immediatePool, 1, &cmd);
}

void imageBarrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess,
    VkImageAspectFlags aspect
) {
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    const VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

void endRenderingIfActive() {
    if(!gCtx.renderingActive) {
        return;
    }

    vkCmdEndRendering(vkCommandBuffer());
    gCtx.renderingActive = false;
    gCtx.renderingToSwapchain = false;

    // You cannot change an attachment's layout while it is being rendered to.
    // After EndRendering, the offscreen colors are just images: make them
    // sampleable so the next pass (tonemap, blit) can bind them as textures.
    const VkCommandBuffer cmd = vkCommandBuffer();
    for(u32 i = 0; i != gCtx.renderingColorCount; ++i) {
        Texture* tex = gCtx.renderingColors[i];
        if(!tex || tex->vkLayout() != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            continue;
        }
        imageBarrier(
            cmd,
            tex->vkImage(),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );
        tex->setVkLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    gCtx.renderingColorCount = 0;
}

static void transitionSwapchainToPresent() {
    if(gCtx.swapchainLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        return;
    }

    imageBarrier(
        vkCommandBuffer(),
        gCtx.swapchainImages[gCtx.imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        0,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    gCtx.swapchainLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

void vkInit(GLFWwindow* window) {
    ALWAYS_ASSERT(window, "initGraphics requires a GLFW window");
    gCtx.window = window;

    vkCheck(volkInitialize());

    u32 glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    ALWAYS_ASSERT(glfwExts && glfwExtCount, "GLFW was not built with Vulkan support");

    std::vector<const char*> instanceExts(glfwExts, glfwExts + glfwExtCount);

#ifdef NEBULA_DEBUG
    const bool useValidation = hasInstanceLayer("VK_LAYER_KHRONOS_validation");
    if(!useValidation) {
        std::cerr << "VK_LAYER_KHRONOS_validation not found; continuing without it" << std::endl;
    }
    instanceExts.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    const VkDebugUtilsMessengerCreateInfoEXT debugCi = debugMessengerInfo();
#endif

    const VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "nebula",
        .apiVersion = VK_API_VERSION_1_3,
    };

    const VkInstanceCreateInfo instanceCi{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifdef NEBULA_DEBUG
        .pNext = &debugCi,
#endif
        .pApplicationInfo = &appInfo,
#ifdef NEBULA_DEBUG
        .enabledLayerCount = useValidation ? 1u : 0u,
        .ppEnabledLayerNames = useValidation ? &validationLayer : nullptr,
#endif
        .enabledExtensionCount = u32(instanceExts.size()),
        .ppEnabledExtensionNames = instanceExts.data(),
    };
    vkCheck(vkCreateInstance(&instanceCi, nullptr, &gCtx.instance));
    volkLoadInstance(gCtx.instance);

#ifdef NEBULA_DEBUG
    vkCheck(vkCreateDebugUtilsMessengerEXT(gCtx.instance, &debugCi, nullptr, &gCtx.debugMessenger));
#endif

    vkCheck(glfwCreateWindowSurface(gCtx.instance, window, nullptr, &gCtx.surface));

    u32 deviceCount = 0;
    vkCheck(vkEnumeratePhysicalDevices(gCtx.instance, &deviceCount, nullptr));
    ALWAYS_ASSERT(deviceCount, "No Vulkan-capable GPUs found");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkCheck(vkEnumeratePhysicalDevices(gCtx.instance, &deviceCount, devices.data()));

    int bestScore = 0;
    for(VkPhysicalDevice physical : devices) {
        u32 family = 0;
        const int score = rateDevice(physical, gCtx.surface, &family);
        if(score > bestScore) {
            bestScore = score;
            gCtx.physicalDevice = physical;
            gCtx.graphicsQueueFamily = family;
        }
    }
    ALWAYS_ASSERT(gCtx.physicalDevice, "No suitable Vulkan 1.3 GPU with graphics+present and swapchain");

    VkPhysicalDeviceProperties2 props{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    };
    vkGetPhysicalDeviceProperties2(gCtx.physicalDevice, &props);
    gCtx.deviceName = props.properties.deviceName;
    gCtx.timestampPeriod = props.properties.limits.timestampPeriod;
    std::cout << "Vulkan 1.3 initialized on " << gCtx.deviceName << std::endl;

    const float queuePriority = 1.0f;
    const VkDeviceQueueCreateInfo queueCi{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = gCtx.graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    const char* deviceExts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    };

    VkPhysicalDeviceVulkan13Features vk13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features vk12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vk13Features,
        .hostQueryReset = VK_TRUE,
    };
    VkPhysicalDeviceVulkan11Features vk11Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &vk12Features,
        .shaderDrawParameters = VK_TRUE,
    };
    const VkPhysicalDeviceFeatures vk10Features{
        .samplerAnisotropy = VK_TRUE,
    };

    const VkDeviceCreateInfo deviceCi{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vk11Features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCi,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = deviceExts,
        .pEnabledFeatures = &vk10Features,
    };
    vkCheck(vkCreateDevice(gCtx.physicalDevice, &deviceCi, nullptr, &gCtx.device));
    vkGetDeviceQueue(gCtx.device, gCtx.graphicsQueueFamily, 0, &gCtx.graphicsQueue);
    volkLoadDevice(gCtx.device);

    const VmaVulkanFunctions vmaFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    VmaAllocatorCreateInfo allocatorCi{
        .physicalDevice = gCtx.physicalDevice,
        .device = gCtx.device,
        .pVulkanFunctions = &vmaFunctions,
        .instance = gCtx.instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    vkCheck(vmaCreateAllocator(&allocatorCi, &gCtx.allocator));

    createSwapchain();
    createFrames();
    createTimestampPools();
    createImmediatePool();
    createPipelineLayout();
    createSamplers();
    createDescriptorPools();
    createFallbackSampledTexture();
    gCtx.frameIndex = 0;
}

void beginFrame() {
    ALWAYS_ASSERT(!gCtx.frameActive, "beginFrame called twice without endFrame");

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(gCtx.window, &width, &height);
    if(width == 0 || height == 0) {
        return;
    }

    if(u32(width) != gCtx.swapchainExtent.width || u32(height) != gCtx.swapchainExtent.height) {
        recreateSwapchain();
    }

    InFlightFrame& frame = gCtx.frames[gCtx.frameIndex];
    vkCheck(vkWaitForFences(gCtx.device, 1, &frame.submitted, VK_TRUE, UINT64_MAX));
    flushFrameDeletions(gCtx.frameIndex);
    resetTimestampQueries();
    vkCheck(vkResetFences(gCtx.device, 1, &frame.submitted));
    vkCheck(vkResetCommandPool(gCtx.device, frame.commandPool, 0));

    VkResult acquired = vkAcquireNextImageKHR(
        gCtx.device,
        gCtx.swapchain,
        UINT64_MAX,
        frame.acquire,
        VK_NULL_HANDLE,
        &gCtx.imageIndex
    );
    if(acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        acquired = vkAcquireNextImageKHR(
            gCtx.device,
            gCtx.swapchain,
            UINT64_MAX,
            frame.acquire,
            VK_NULL_HANDLE,
            &gCtx.imageIndex
        );
    }
    ALWAYS_ASSERT(acquired == VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR, "vkAcquireNextImageKHR failed");

    const VkCommandBufferBeginInfo beginCi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkCheck(vkBeginCommandBuffer(frame.commandBuffer, &beginCi));

    gCtx.frameActive = true;
    gCtx.renderingActive = false;
    gCtx.renderingToSwapchain = false;
    gCtx.swapchainLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void endFrame() {
    if(!gCtx.frameActive) {
        return;
    }

    InFlightFrame& frame = gCtx.frames[gCtx.frameIndex];
    VkSemaphore& renderSem = gCtx.swapchainRenderSemaphores[gCtx.imageIndex];
    // ImGui draws into the swapchain rendering left open by blitToScreen.
    endRenderingIfActive();
    transitionSwapchainToPresent();
    vkCheck(vkEndCommandBuffer(frame.commandBuffer));

    const VkSemaphoreSubmitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.acquire,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkCommandBufferSubmitInfo cmdInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
    };
    const VkSemaphoreSubmitInfo signal{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderSem,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal,
    };
    vkCheck(vkQueueSubmit2(gCtx.graphicsQueue, 1, &submit, frame.submitted));

    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderSem,
        .swapchainCount = 1,
        .pSwapchains = &gCtx.swapchain,
        .pImageIndices = &gCtx.imageIndex,
    };
    const VkResult presented = vkQueuePresentKHR(gCtx.graphicsQueue, &present);
    if(presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else {
        vkCheck(presented);
    }

    gCtx.frameActive = false;
    gCtx.frameIndex = (gCtx.frameIndex + 1) % framesInFlight;
}

void vkDestroy() {
    if(gCtx.device) {
        waitForGpuIdle();
        flushAllDeletions();
    }
    destroyFrames();
    destroyTimestampPools();
    destroySwapchain();
    if(gCtx.immediatePool) {
        vkDestroyCommandPool(gCtx.device, gCtx.immediatePool, nullptr);
        gCtx.immediatePool = VK_NULL_HANDLE;
    }
    if(gCtx.frameDescriptorPool) {
        vkDestroyDescriptorPool(gCtx.device, gCtx.frameDescriptorPool, nullptr);
        gCtx.frameDescriptorPool = VK_NULL_HANDLE;
        for(InFlightFrame& frame : gCtx.frames) {
            frame.frameDescriptorSet = VK_NULL_HANDLE;
        }
    }
    if(gCtx.fallbackSampledView) {
        vkDestroyImageView(gCtx.device, gCtx.fallbackSampledView, nullptr);
        gCtx.fallbackSampledView = VK_NULL_HANDLE;
    }
    if(gCtx.fallbackSampledImage) {
        vmaDestroyImage(gCtx.allocator, gCtx.fallbackSampledImage, gCtx.fallbackSampledAllocation);
        gCtx.fallbackSampledImage = VK_NULL_HANDLE;
        gCtx.fallbackSampledAllocation = nullptr;
    }
    if(gCtx.samplerRepeat) {
        vkDestroySampler(gCtx.device, gCtx.samplerRepeat, nullptr);
        gCtx.samplerRepeat = VK_NULL_HANDLE;
    }
    if(gCtx.samplerClamp) {
        vkDestroySampler(gCtx.device, gCtx.samplerClamp, nullptr);
        gCtx.samplerClamp = VK_NULL_HANDLE;
    }
    if(gCtx.pipelineLayout) {
        vkDestroyPipelineLayout(gCtx.device, gCtx.pipelineLayout, nullptr);
        gCtx.pipelineLayout = VK_NULL_HANDLE;
    }
    if(gCtx.passSetLayout) {
        vkDestroyDescriptorSetLayout(gCtx.device, gCtx.passSetLayout, nullptr);
        gCtx.passSetLayout = VK_NULL_HANDLE;
    }
    if(gCtx.frameSetLayout) {
        vkDestroyDescriptorSetLayout(gCtx.device, gCtx.frameSetLayout, nullptr);
        gCtx.frameSetLayout = VK_NULL_HANDLE;
    }
    if(gCtx.allocator) {
        vmaDestroyAllocator(gCtx.allocator);
        gCtx.allocator = VK_NULL_HANDLE;
    }
    if(gCtx.device) {
        vkDestroyDevice(gCtx.device, nullptr);
        gCtx.device = VK_NULL_HANDLE;
    }
    if(gCtx.surface) {
        vkDestroySurfaceKHR(gCtx.instance, gCtx.surface, nullptr);
        gCtx.surface = VK_NULL_HANDLE;
    }
#ifdef NEBULA_DEBUG
    if(gCtx.debugMessenger) {
        vkDestroyDebugUtilsMessengerEXT(gCtx.instance, gCtx.debugMessenger, nullptr);
        gCtx.debugMessenger = VK_NULL_HANDLE;
    }
#endif
    if(gCtx.instance) {
        vkDestroyInstance(gCtx.instance, nullptr);
        gCtx.instance = VK_NULL_HANDLE;
    }
    gCtx = {};
}

}