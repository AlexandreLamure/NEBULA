#define VMA_IMPLEMENTATION // The vk_mem_alloc.h included in VkContext.h will compile the bodies of the VMA functions (STB-style implementation)
#include "VkContext.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "Program.h"
#include "Texture.h"
#include "graphics.h"
#include "ImageFormat.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace OM3D {

// Singleton containing the Vulkan context.
static GraphicsContext g_ctx;
GraphicsContext& ctx() {
    return g_ctx;
}

#ifdef OM3D_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*
) {
    if(severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return VK_FALSE;
    }

    const bool is_error = (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    (is_error ? std::cerr : std::cout) << (is_error ? "[VK][ERROR] " : "[VK] ")
                                       << data->pMessage << std::endl;

    if(is_error) {
        break_in_debugger();
    }
    return VK_FALSE;
}

static bool has_instance_layer(const char* name) {
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

static VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info() {
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };
}
#endif

static bool has_device_extension(VkPhysicalDevice physical, const char* name) {
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

static bool find_graphics_present_queue(VkPhysicalDevice physical, VkSurfaceKHR surface, u32* out_family) {
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
            *out_family = i;
            return true;
        }
    }
    return false;
}

static int rate_device(VkPhysicalDevice physical, VkSurfaceKHR surface, u32* out_family) {
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

    if(!has_device_extension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return 0;
    }

    if(!find_graphics_present_queue(physical, surface, out_family)) {
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

static void destroy_swapchain() {
    for(VkSemaphore sem : g_ctx.swapchain_render_semaphores) {
        vkDestroySemaphore(g_ctx.device, sem, nullptr);
    }
    g_ctx.swapchain_render_semaphores.clear();

    for(VkImageView view : g_ctx.swapchain_views) {
        vkDestroyImageView(g_ctx.device, view, nullptr);
    }
    g_ctx.swapchain_views.clear();
    g_ctx.swapchain_images.clear();

    if(g_ctx.swapchain) {
        vkDestroySwapchainKHR(g_ctx.device, g_ctx.swapchain, nullptr);
        g_ctx.swapchain = VK_NULL_HANDLE;
    }
}

static void destroy_frames() {
    for(InFlightFrame& frame : g_ctx.frames) {
        if(frame.command_pool) {
            vkDestroyCommandPool(g_ctx.device, frame.command_pool, nullptr);
        }
        if(frame.submitted) {
            vkDestroyFence(g_ctx.device, frame.submitted, nullptr);
        }
        if(frame.acquire) {
            vkDestroySemaphore(g_ctx.device, frame.acquire, nullptr);
        }
        if(frame.descriptor_pool) {
            vkDestroyDescriptorPool(g_ctx.device, frame.descriptor_pool, nullptr);
        }
        frame = {};
    }
}

static VkSurfaceFormatKHR pick_surface_format(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    u32 count = 0;
    vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(count);
    vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()));

    for(const VkSurfaceFormatKHR& format : formats) {
        if(format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    ALWAYS_ASSERT(count, "No swapchain formats available");
    return formats[0];
}

static VkExtent2D pick_swapchain_extent(const VkSurfaceCapabilitiesKHR& caps) {
    // Wayland (and some other platforms) reports currentExtent as 0xFFFFFFFF:
    // the window size is not known to the driver, so we must pass GLFW's framebuffer size.
    if(caps.currentExtent.width != 0xFFFFFFFFu) {
        return caps.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(g_ctx.window, &width, &height);

    VkExtent2D extent{u32(width), u32(height)};
    extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, extent.width));
    extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, extent.height));
    return extent;
}

static void create_swapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_ctx.physical_device, g_ctx.surface, &caps));

    const VkSurfaceFormatKHR surface_format = pick_surface_format(g_ctx.physical_device, g_ctx.surface);
    const VkExtent2D extent = pick_swapchain_extent(caps);
    ALWAYS_ASSERT(extent.width && extent.height, "Swapchain extent is zero");

    u32 image_count = caps.minImageCount + 1;
    if(caps.maxImageCount && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    const VkSwapchainCreateInfoKHR swapchain_ci{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g_ctx.surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    vk_check(vkCreateSwapchainKHR(g_ctx.device, &swapchain_ci, nullptr, &g_ctx.swapchain));

    g_ctx.swapchain_format = surface_format.format;
    g_ctx.swapchain_extent = extent;
    g_ctx.rendering_color_format = surface_format.format;

    u32 actual_count = 0;
    vk_check(vkGetSwapchainImagesKHR(g_ctx.device, g_ctx.swapchain, &actual_count, nullptr));
    g_ctx.swapchain_images.resize(actual_count);
    vk_check(vkGetSwapchainImagesKHR(g_ctx.device, g_ctx.swapchain, &actual_count, g_ctx.swapchain_images.data()));

    g_ctx.swapchain_views.resize(actual_count);
    for(u32 i = 0; i != actual_count; ++i) {
        const VkImageViewCreateInfo view_ci{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = g_ctx.swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = g_ctx.swapchain_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        vk_check(vkCreateImageView(g_ctx.device, &view_ci, nullptr, &g_ctx.swapchain_views[i]));
    }

    g_ctx.swapchain_render_semaphores.resize(actual_count);
    const VkSemaphoreCreateInfo sem_ci{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for(u32 i = 0; i != actual_count; ++i) {
        vk_check(vkCreateSemaphore(g_ctx.device, &sem_ci, nullptr, &g_ctx.swapchain_render_semaphores[i]));
    }
}

void wait_for_gpu_idle() {
    if(g_ctx.device) {
        vkDeviceWaitIdle(g_ctx.device);
    }
}

static void recreate_swapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(g_ctx.window, &width, &height);
    if(width == 0 || height == 0) {
        return;
    }

    wait_for_gpu_idle();
    destroy_swapchain();
    create_swapchain();
}

static void create_frames() {
    for(InFlightFrame& frame : g_ctx.frames) {
        const VkCommandPoolCreateInfo pool_ci{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = g_ctx.graphics_queue_family,
        };
        vk_check(vkCreateCommandPool(g_ctx.device, &pool_ci, nullptr, &frame.command_pool));

        const VkCommandBufferAllocateInfo alloc_ci{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vk_check(vkAllocateCommandBuffers(g_ctx.device, &alloc_ci, &frame.command_buffer));

        const VkFenceCreateInfo fence_ci{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        vk_check(vkCreateFence(g_ctx.device, &fence_ci, nullptr, &frame.submitted));

        const VkSemaphoreCreateInfo sem_ci{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        vk_check(vkCreateSemaphore(g_ctx.device, &sem_ci, nullptr, &frame.acquire));
    }
}

static void create_timestamp_pools() {
    u32 family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_ctx.physical_device, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(g_ctx.physical_device, &family_count, families.data());
    g_ctx.timestamp_valid_bits = families[g_ctx.graphics_queue_family].timestampValidBits;

    if(!g_ctx.timestamp_valid_bits) {
        return;
    }

    for(u32 i = 0; i != frames_in_flight; ++i) {
        const VkQueryPoolCreateInfo pool_ci{
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = timestamp_queries_per_frame,
        };
        vk_check(vkCreateQueryPool(g_ctx.device, &pool_ci, nullptr, &g_ctx.timestamp_pools[i]));
    }
}

static void destroy_timestamp_pools() {
    for(u32 i = 0; i != frames_in_flight; ++i) {
        if(g_ctx.timestamp_pools[i]) {
            vkDestroyQueryPool(g_ctx.device, g_ctx.timestamp_pools[i], nullptr);
            g_ctx.timestamp_pools[i] = VK_NULL_HANDLE;
        }
    }
}

static void create_pipeline_layout() {
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
    };
    const VkDescriptorSetLayoutCreateInfo set_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = descriptor_binding_count,
        .pBindings = bindings,
    };
    vk_check(vkCreateDescriptorSetLayout(g_ctx.device, &set_ci, nullptr, &g_ctx.descriptor_set_layout));

    const VkPushConstantRange push{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    const VkPipelineLayoutCreateInfo layout_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &g_ctx.descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    vk_check(vkCreatePipelineLayout(g_ctx.device, &layout_ci, nullptr, &g_ctx.pipeline_layout));
}

static void create_samplers() {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(g_ctx.physical_device, &props);

    const auto make_sampler = [&](VkSamplerAddressMode address_mode) {
        const VkSamplerCreateInfo ci{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = address_mode,
            .addressModeV = address_mode,
            .addressModeW = address_mode,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = props.limits.maxSamplerAnisotropy,
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE,
        };
        VkSampler sampler = VK_NULL_HANDLE;
        vk_check(vkCreateSampler(g_ctx.device, &ci, nullptr, &sampler));
        return sampler;
    };

    g_ctx.sampler_repeat = make_sampler(VK_SAMPLER_ADDRESS_MODE_REPEAT);
    g_ctx.sampler_clamp = make_sampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

static void create_fallback_sampled_texture() {
    const VkImageCreateInfo image_ci{
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
    const VmaAllocationCreateInfo alloc_ci{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    vk_check(vmaCreateImage(g_ctx.allocator, &image_ci, &alloc_ci, &g_ctx.fallback_sampled_image, &g_ctx.fallback_sampled_allocation, nullptr));

    const VkImageViewCreateInfo view_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = g_ctx.fallback_sampled_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vk_check(vkCreateImageView(g_ctx.device, &view_ci, nullptr, &g_ctx.fallback_sampled_view));

    const u8 black[4] = {0, 0, 0, 255};

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_allocation = nullptr;
    {
        const VkBufferCreateInfo buffer_ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(black),
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo staging_alloc{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo info{};
        vk_check(vmaCreateBuffer(g_ctx.allocator, &buffer_ci, &staging_alloc, &staging_buffer, &staging_allocation, &info));
        std::memcpy(info.pMappedData, black, sizeof(black));
    }

    immediate_submit([&](VkCommandBuffer cmd) {
        image_barrier(
            cmd,
            g_ctx.fallback_sampled_image,
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
        vkCmdCopyBufferToImage(cmd, staging_buffer, g_ctx.fallback_sampled_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        image_barrier(
            cmd,
            g_ctx.fallback_sampled_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
    });

    vmaDestroyBuffer(g_ctx.allocator, staging_buffer, staging_allocation);
}

static void create_descriptor_pool(VkDescriptorPool& pool) {
    static constexpr u32 max_sets_per_frame = 512;
    const VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_sets_per_frame},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, max_sets_per_frame},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_sets_per_frame * gl_texture_slot_count},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, max_sets_per_frame},
    };
    const VkDescriptorPoolCreateInfo pool_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = max_sets_per_frame,
        .poolSizeCount = u32(std::size(pool_sizes)),
        .pPoolSizes = pool_sizes,
    };
    vk_check(vkCreateDescriptorPool(g_ctx.device, &pool_ci, nullptr, &pool));
}

static void create_descriptor_pools() {
    for(InFlightFrame& frame : g_ctx.frames) {
        create_descriptor_pool(frame.descriptor_pool);
    }
    create_descriptor_pool(g_ctx.immediate_descriptor_pool);
}

static VkSampler sampler_for_texture(const Texture* texture) {
    if(!texture) {
        return g_ctx.sampler_repeat;
    }
    return texture->wrap_mode() == WrapMode::Clamp ? g_ctx.sampler_clamp : g_ctx.sampler_repeat;
}

static VkImageLayout sampled_layout_for_texture(const Texture* texture) {
    if(!texture || texture->vk_layout() == VK_IMAGE_LAYOUT_UNDEFINED) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return texture->vk_layout();
}

static const Texture* default_texture_for_slot(u32 gl_slot) {
    switch(gl_slot) {
        case 0:
        case 3:
            return default_white_texture().get();
        case 1:
            return default_normal_texture().get();
        case 2:
            return default_metal_rough_texture().get();
        default:
            return default_black_texture().get();
    }
}

static VkDescriptorImageInfo sampled_image_info(u32 gl_slot) {
    const Texture* texture = g_ctx.bound_textures[gl_slot].texture;
    if(!texture) {
        texture = default_texture_for_slot(gl_slot);
    }

    VkImageView view = g_ctx.fallback_sampled_view;
    if(texture && texture->vk_view()) {
        view = texture->vk_view();
    }

    return {
        .sampler = sampler_for_texture(texture),
        .imageView = view,
        .imageLayout = sampled_layout_for_texture(texture),
    };
}

void flush_descriptor_bindings() {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if(g_ctx.immediate_cmd) {
        pool = g_ctx.immediate_descriptor_pool;
    } else if(g_ctx.frame_active) {
        pool = g_ctx.frames[g_ctx.frame_index].descriptor_pool;
    }
    if(!pool || !g_ctx.pipeline_layout) {
        return;
    }

    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkDescriptorSetAllocateInfo alloc_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &g_ctx.descriptor_set_layout,
    };
    vk_check(vkAllocateDescriptorSets(g_ctx.device, &alloc_ci, &set));

    VkDescriptorBufferInfo buffer_infos[2] = {};
    VkWriteDescriptorSet writes[descriptor_binding_count] = {};
    u32 write_count = 0;

    for(u32 binding = 0; binding < 2; ++binding) {
        const BoundBuffer& bound = g_ctx.bound_descriptors[binding];
        if(!bound.buffer) {
            continue;
        }

        buffer_infos[binding] = {
            .buffer = bound.buffer,
            .offset = 0,
            .range = bound.size ? bound.size : VK_WHOLE_SIZE,
        };
        writes[write_count++] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &buffer_infos[binding],
        };
    }

    VkDescriptorImageInfo sampled_infos[gl_texture_slot_count] = {};
    for(u32 gl_slot = 0; gl_slot != gl_texture_slot_count; ++gl_slot) {
        sampled_infos[gl_slot] = sampled_image_info(gl_slot);
        writes[write_count++] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = descriptor_texture_binding_base + gl_slot,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &sampled_infos[gl_slot],
        };
    }

    VkDescriptorImageInfo storage_info{};
    const Texture* storage_texture = g_ctx.bound_storage_image.texture;
    if(storage_texture && storage_texture->vk_storage_view()) {
        storage_info = {
            .imageView = storage_texture->vk_storage_view(),
            .imageLayout = g_ctx.bound_storage_image.layout,
        };
    } else {
        storage_info = {
            .imageView = g_ctx.fallback_sampled_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    }
    writes[write_count++] = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 8,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &storage_info,
    };

    vkUpdateDescriptorSets(g_ctx.device, write_count, writes, 0, nullptr);

    const VkPipelineBindPoint bind_point =
        (g_ctx.bound_program && g_ctx.bound_program->is_compute())
            ? VK_PIPELINE_BIND_POINT_COMPUTE
            : VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkCmdBindDescriptorSets(
        vk_command_buffer(),
        bind_point,
        g_ctx.pipeline_layout,
        0,
        1,
        &set,
        0,
        nullptr
    );
}

void dispatch_compute(u32 x, u32 y, u32 z) {
    if(!vk_is_recording()) {
        return;
    }
    flush_descriptor_bindings();
    vkCmdDispatch(vk_command_buffer(), x, y, z);
}

static void create_immediate_pool() {
    const VkCommandPoolCreateInfo pool_ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = g_ctx.graphics_queue_family,
    };
    vk_check(vkCreateCommandPool(g_ctx.device, &pool_ci, nullptr, &g_ctx.immediate_pool));
}

static u32 pending_deletion_slot() {
    // During a frame, objects may still be referenced by this slot's command buffer.
    // Between frames, the last submitted slot is the one that may still be on the GPU.
    if(g_ctx.frame_active) {
        return g_ctx.frame_index;
    }
    return (g_ctx.frame_index + frames_in_flight - 1) % frames_in_flight;
}

static void destroy_entry(const DeletionEntry& entry) {
    switch(entry.type) {
        case DeletionEntry::Type::Buffer:
            vmaDestroyBuffer(g_ctx.allocator, entry.buffer, entry.allocation);
            break;
        case DeletionEntry::Type::Image:
            vmaDestroyImage(g_ctx.allocator, entry.image, entry.allocation);
            break;
        case DeletionEntry::Type::ImageView:
            vkDestroyImageView(g_ctx.device, entry.image_view, nullptr);
            break;
        case DeletionEntry::Type::Sampler:
            vkDestroySampler(g_ctx.device, entry.sampler, nullptr);
            break;
        case DeletionEntry::Type::Pipeline:
            vkDestroyPipeline(g_ctx.device, entry.pipeline, nullptr);
            break;
        case DeletionEntry::Type::ShaderModule:
            vkDestroyShaderModule(g_ctx.device, entry.shader_module, nullptr);
            break;
    }
}

void flush_frame_deletions(u32 frame_slot) {
    std::vector<DeletionEntry>& queue = g_ctx.deletions[frame_slot];
    for(auto it = queue.rbegin(); it != queue.rend(); ++it) {
        destroy_entry(*it);
    }
    queue.clear();
}

void flush_all_deletions() {
    for(u32 i = 0; i != frames_in_flight; ++i) {
        flush_frame_deletions(i);
    }
}

static void enqueue_deletion(DeletionEntry entry) {
    if(!g_ctx.device) {
        return;
    }
    g_ctx.deletions[pending_deletion_slot()].push_back(entry);
}

void defer_destroy(VkBuffer buffer, VmaAllocation allocation) {
    if(!buffer) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::Buffer;
    entry.allocation = allocation;
    entry.buffer = buffer;
    enqueue_deletion(entry);
}

void defer_destroy(VkImage image, VmaAllocation allocation) {
    if(!image) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::Image;
    entry.allocation = allocation;
    entry.image = image;
    enqueue_deletion(entry);
}

void defer_destroy(VkImageView view) {
    if(!view) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::ImageView;
    entry.image_view = view;
    enqueue_deletion(entry);
}

void defer_destroy(VkSampler sampler) {
    if(!sampler) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::Sampler;
    entry.sampler = sampler;
    enqueue_deletion(entry);
}

void defer_destroy(VkPipeline pipeline) {
    if(!pipeline) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::Pipeline;
    entry.pipeline = pipeline;
    enqueue_deletion(entry);
}

void defer_destroy(VkShaderModule module) {
    if(!module) {
        return;
    }
    DeletionEntry entry{};
    entry.type = DeletionEntry::Type::ShaderModule;
    entry.shader_module = module;
    enqueue_deletion(entry);
}

void immediate_submit(std::function<void(VkCommandBuffer)>&& record) {
    ALWAYS_ASSERT(g_ctx.immediate_pool && g_ctx.graphics_queue, "immediate_submit called before vk_init");
    if(g_ctx.immediate_descriptor_pool) {
        vk_check(vkResetDescriptorPool(g_ctx.device, g_ctx.immediate_descriptor_pool, 0));
    }

    const VkCommandBufferAllocateInfo alloc_ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_ctx.immediate_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vk_check(vkAllocateCommandBuffers(g_ctx.device, &alloc_ci, &cmd));

    const VkCommandBufferBeginInfo begin_ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk_check(vkBeginCommandBuffer(cmd, &begin_ci));

    // Install as the current command buffer so Program::bind / dispatch_compute
    // (the GL-style API) record here instead of into a frame that is not active.
    const VkCommandBuffer prev_immediate = g_ctx.immediate_cmd;
    g_ctx.immediate_cmd = cmd;
    record(cmd);
    g_ctx.immediate_cmd = prev_immediate;

    vk_check(vkEndCommandBuffer(cmd));

    const VkFenceCreateInfo fence_ci{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence = VK_NULL_HANDLE;
    vk_check(vkCreateFence(g_ctx.device, &fence_ci, nullptr, &fence));

    const VkCommandBufferSubmitInfo cmd_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmd_info,
    };
    vk_check(vkQueueSubmit2(g_ctx.graphics_queue, 1, &submit, fence));
    vk_check(vkWaitForFences(g_ctx.device, 1, &fence, VK_TRUE, UINT64_MAX));

    vkDestroyFence(g_ctx.device, fence, nullptr);
    vkFreeCommandBuffers(g_ctx.device, g_ctx.immediate_pool, 1, &cmd);
}

void image_barrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access,
    VkImageAspectFlags aspect
) {
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
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

void end_rendering_if_active() {
    if(!g_ctx.rendering_active) {
        return;
    }

    vkCmdEndRendering(vk_command_buffer());
    g_ctx.rendering_active = false;
    g_ctx.rendering_to_swapchain = false;

    // You cannot change an attachment's layout while it is being rendered to.
    // After EndRendering, the offscreen colors are just images: make them
    // sampleable so the next pass (tonemap, blit) can bind them as textures.
    const VkCommandBuffer cmd = vk_command_buffer();
    for(u32 i = 0; i != g_ctx.rendering_color_count; ++i) {
        Texture* tex = g_ctx.rendering_colors[i];
        if(!tex || tex->vk_layout() != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            continue;
        }
        image_barrier(
            cmd,
            tex->vk_image(),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );
        tex->set_vk_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    g_ctx.rendering_color_count = 0;
}

static void transition_swapchain_to_present() {
    if(g_ctx.swapchain_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        return;
    }

    image_barrier(
        vk_command_buffer(),
        g_ctx.swapchain_images[g_ctx.image_index],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        0,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    g_ctx.swapchain_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

void vk_init(GLFWwindow* window) {
    ALWAYS_ASSERT(window, "init_graphics requires a GLFW window");
    g_ctx.window = window;

    vk_check(volkInitialize());

    u32 glfw_ext_count = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    ALWAYS_ASSERT(glfw_exts && glfw_ext_count, "GLFW was not built with Vulkan support");

    std::vector<const char*> instance_exts(glfw_exts, glfw_exts + glfw_ext_count);

#ifdef OM3D_DEBUG
    const bool use_validation = has_instance_layer("VK_LAYER_KHRONOS_validation");
    if(!use_validation) {
        std::cerr << "VK_LAYER_KHRONOS_validation not found; continuing without it" << std::endl;
    }
    instance_exts.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    const char* validation_layer = "VK_LAYER_KHRONOS_validation";
    const VkDebugUtilsMessengerCreateInfoEXT debug_ci = debug_messenger_info();
#endif

    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "OM3D",
        .apiVersion = VK_API_VERSION_1_3,
    };

    const VkInstanceCreateInfo instance_ci{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifdef OM3D_DEBUG
        .pNext = &debug_ci,
#endif
        .pApplicationInfo = &app_info,
#ifdef OM3D_DEBUG
        .enabledLayerCount = use_validation ? 1u : 0u,
        .ppEnabledLayerNames = use_validation ? &validation_layer : nullptr,
#endif
        .enabledExtensionCount = u32(instance_exts.size()),
        .ppEnabledExtensionNames = instance_exts.data(),
    };
    vk_check(vkCreateInstance(&instance_ci, nullptr, &g_ctx.instance));
    volkLoadInstance(g_ctx.instance);

#ifdef OM3D_DEBUG
    vk_check(vkCreateDebugUtilsMessengerEXT(g_ctx.instance, &debug_ci, nullptr, &g_ctx.debug_messenger));
#endif

    vk_check(glfwCreateWindowSurface(g_ctx.instance, window, nullptr, &g_ctx.surface));

    u32 device_count = 0;
    vk_check(vkEnumeratePhysicalDevices(g_ctx.instance, &device_count, nullptr));
    ALWAYS_ASSERT(device_count, "No Vulkan-capable GPUs found");
    std::vector<VkPhysicalDevice> devices(device_count);
    vk_check(vkEnumeratePhysicalDevices(g_ctx.instance, &device_count, devices.data()));

    int best_score = 0;
    for(VkPhysicalDevice physical : devices) {
        u32 family = 0;
        const int score = rate_device(physical, g_ctx.surface, &family);
        if(score > best_score) {
            best_score = score;
            g_ctx.physical_device = physical;
            g_ctx.graphics_queue_family = family;
        }
    }
    ALWAYS_ASSERT(g_ctx.physical_device, "No suitable Vulkan 1.3 GPU with graphics+present and swapchain");

    VkPhysicalDeviceProperties2 props{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    };
    vkGetPhysicalDeviceProperties2(g_ctx.physical_device, &props);
    g_ctx.device_name = props.properties.deviceName;
    g_ctx.timestamp_period = props.properties.limits.timestampPeriod;
    std::cout << "Vulkan 1.3 initialized on " << g_ctx.device_name << std::endl;

    const float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_ci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = g_ctx.graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char* device_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceVulkan13Features vk13_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features vk12_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vk13_features,
        .hostQueryReset = VK_TRUE,
    };
    VkPhysicalDeviceVulkan11Features vk11_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &vk12_features,
        .shaderDrawParameters = VK_TRUE,
    };
    const VkPhysicalDeviceFeatures vk10_features{
        .samplerAnisotropy = VK_TRUE,
    };

    const VkDeviceCreateInfo device_ci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vk11_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_ci,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_exts,
        .pEnabledFeatures = &vk10_features,
    };
    vk_check(vkCreateDevice(g_ctx.physical_device, &device_ci, nullptr, &g_ctx.device));
    vkGetDeviceQueue(g_ctx.device, g_ctx.graphics_queue_family, 0, &g_ctx.graphics_queue);
    volkLoadDevice(g_ctx.device);

    const VmaVulkanFunctions vma_functions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    VmaAllocatorCreateInfo allocator_ci{
        .physicalDevice = g_ctx.physical_device,
        .device = g_ctx.device,
        .pVulkanFunctions = &vma_functions,
        .instance = g_ctx.instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    vk_check(vmaCreateAllocator(&allocator_ci, &g_ctx.allocator));

    create_swapchain();
    create_frames();
    create_timestamp_pools();
    create_immediate_pool();
    create_pipeline_layout();
    create_samplers();
    create_fallback_sampled_texture();
    create_descriptor_pools();
}

void begin_frame() {
    ALWAYS_ASSERT(!g_ctx.frame_active, "begin_frame called twice without end_frame");

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(g_ctx.window, &width, &height);
    if(width == 0 || height == 0) {
        return;
    }

    if(u32(width) != g_ctx.swapchain_extent.width || u32(height) != g_ctx.swapchain_extent.height) {
        recreate_swapchain();
    }

    InFlightFrame& frame = g_ctx.frames[g_ctx.frame_index];
    g_ctx.bound_program = nullptr;
    g_ctx.bound_vertex = {};
    g_ctx.bound_index = {};
    for(BoundBuffer& bound : g_ctx.bound_descriptors) {
        bound = {};
    }
    for(BoundSampledTexture& bound : g_ctx.bound_textures) {
        bound = {};
    }
    g_ctx.bound_storage_image = {};
    vk_check(vkWaitForFences(g_ctx.device, 1, &frame.submitted, VK_TRUE, UINT64_MAX));
    if(frame.descriptor_pool) {
        vk_check(vkResetDescriptorPool(g_ctx.device, frame.descriptor_pool, 0));
    }
    flush_frame_deletions(g_ctx.frame_index);
    reset_timestamp_queries();
    vk_check(vkResetFences(g_ctx.device, 1, &frame.submitted));
    vk_check(vkResetCommandPool(g_ctx.device, frame.command_pool, 0));

    VkResult acquired = vkAcquireNextImageKHR(
        g_ctx.device,
        g_ctx.swapchain,
        UINT64_MAX,
        frame.acquire,
        VK_NULL_HANDLE,
        &g_ctx.image_index
    );
    if(acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        acquired = vkAcquireNextImageKHR(
            g_ctx.device,
            g_ctx.swapchain,
            UINT64_MAX,
            frame.acquire,
            VK_NULL_HANDLE,
            &g_ctx.image_index
        );
    }
    ALWAYS_ASSERT(acquired == VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR, "vkAcquireNextImageKHR failed");

    const VkCommandBufferBeginInfo begin_ci{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk_check(vkBeginCommandBuffer(frame.command_buffer, &begin_ci));

    g_ctx.frame_active = true;
    g_ctx.rendering_active = false;
    g_ctx.rendering_to_swapchain = false;
    g_ctx.swapchain_layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void end_frame() {
    if(!g_ctx.frame_active) {
        return;
    }

    InFlightFrame& frame = g_ctx.frames[g_ctx.frame_index];
    VkSemaphore& render_sem = g_ctx.swapchain_render_semaphores[g_ctx.image_index];
    // ImGui draws into the swapchain rendering left open by blit_to_screen.
    end_rendering_if_active();
    transition_swapchain_to_present();
    vk_check(vkEndCommandBuffer(frame.command_buffer));

    const VkSemaphoreSubmitInfo wait{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.acquire,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkCommandBufferSubmitInfo cmd_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.command_buffer,
    };
    const VkSemaphoreSubmitInfo signal{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = render_sem,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmd_info,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal,
    };
    vk_check(vkQueueSubmit2(g_ctx.graphics_queue, 1, &submit, frame.submitted));

    const VkPresentInfoKHR present{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_sem,
        .swapchainCount = 1,
        .pSwapchains = &g_ctx.swapchain,
        .pImageIndices = &g_ctx.image_index,
    };
    const VkResult presented = vkQueuePresentKHR(g_ctx.graphics_queue, &present);
    if(presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    } else {
        vk_check(presented);
    }

    g_ctx.frame_active = false;
    g_ctx.frame_index = (g_ctx.frame_index + 1) % frames_in_flight;
}

void vk_destroy() {
    if(g_ctx.device) {
        wait_for_gpu_idle();
        flush_all_deletions();
    }
    destroy_frames();
    destroy_timestamp_pools();
    destroy_swapchain();
    if(g_ctx.immediate_pool) {
        vkDestroyCommandPool(g_ctx.device, g_ctx.immediate_pool, nullptr);
        g_ctx.immediate_pool = VK_NULL_HANDLE;
    }
    if(g_ctx.immediate_descriptor_pool) {
        vkDestroyDescriptorPool(g_ctx.device, g_ctx.immediate_descriptor_pool, nullptr);
        g_ctx.immediate_descriptor_pool = VK_NULL_HANDLE;
    }
    if(g_ctx.fallback_sampled_view) {
        vkDestroyImageView(g_ctx.device, g_ctx.fallback_sampled_view, nullptr);
        g_ctx.fallback_sampled_view = VK_NULL_HANDLE;
    }
    if(g_ctx.fallback_sampled_image) {
        vmaDestroyImage(g_ctx.allocator, g_ctx.fallback_sampled_image, g_ctx.fallback_sampled_allocation);
        g_ctx.fallback_sampled_image = VK_NULL_HANDLE;
        g_ctx.fallback_sampled_allocation = nullptr;
    }
    if(g_ctx.sampler_repeat) {
        vkDestroySampler(g_ctx.device, g_ctx.sampler_repeat, nullptr);
        g_ctx.sampler_repeat = VK_NULL_HANDLE;
    }
    if(g_ctx.sampler_clamp) {
        vkDestroySampler(g_ctx.device, g_ctx.sampler_clamp, nullptr);
        g_ctx.sampler_clamp = VK_NULL_HANDLE;
    }
    if(g_ctx.pipeline_layout) {
        vkDestroyPipelineLayout(g_ctx.device, g_ctx.pipeline_layout, nullptr);
        g_ctx.pipeline_layout = VK_NULL_HANDLE;
    }
    if(g_ctx.descriptor_set_layout) {
        vkDestroyDescriptorSetLayout(g_ctx.device, g_ctx.descriptor_set_layout, nullptr);
        g_ctx.descriptor_set_layout = VK_NULL_HANDLE;
    }
    if(g_ctx.allocator) {
        vmaDestroyAllocator(g_ctx.allocator);
        g_ctx.allocator = VK_NULL_HANDLE;
    }
    if(g_ctx.device) {
        vkDestroyDevice(g_ctx.device, nullptr);
        g_ctx.device = VK_NULL_HANDLE;
    }
    if(g_ctx.surface) {
        vkDestroySurfaceKHR(g_ctx.instance, g_ctx.surface, nullptr);
        g_ctx.surface = VK_NULL_HANDLE;
    }
#ifdef OM3D_DEBUG
    if(g_ctx.debug_messenger) {
        vkDestroyDebugUtilsMessengerEXT(g_ctx.instance, g_ctx.debug_messenger, nullptr);
        g_ctx.debug_messenger = VK_NULL_HANDLE;
    }
#endif
    if(g_ctx.instance) {
        vkDestroyInstance(g_ctx.instance, nullptr);
        g_ctx.instance = VK_NULL_HANDLE;
    }
    g_ctx = {};
}

}