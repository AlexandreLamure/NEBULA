#define VMA_IMPLEMENTATION // The vk_mem_alloc.h included in VkContext.h will compile the bodies of the VMA functions (STB-style implementation)
#include "VkContext.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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
        if(frame.render) {
            vkDestroySemaphore(g_ctx.device, frame.render, nullptr);
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
}

static void recreate_swapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(g_ctx.window, &width, &height);
    if(width == 0 || height == 0) {
        return;
    }

    vkDeviceWaitIdle(g_ctx.device);
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
        vk_check(vkCreateSemaphore(g_ctx.device, &sem_ci, nullptr, &frame.render));
    }
}

static void image_barrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access
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
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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

static void set_swapchain_viewport(VkCommandBuffer cmd) {
    // Vulkan NDC has Y down. A negative viewport height flips Y so shaders can keep OpenGL-style NDC.
    const float height = float(g_ctx.swapchain_extent.height);
    const VkViewport viewport{
        .x = 0.0f,
        .y = height,
        .width = float(g_ctx.swapchain_extent.width),
        .height = -height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor{
        .extent = g_ctx.swapchain_extent,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

static void begin_swapchain_rendering() {
    const VkCommandBuffer cmd = vk_command_buffer();
    const VkImage image = g_ctx.swapchain_images[g_ctx.image_index];

    image_barrier(
        cmd,
        image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    const VkRenderingAttachmentInfo color{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = g_ctx.swapchain_views[g_ctx.image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{0.5f, 0.7f, 0.8f, 1.0f}}},
    };
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = g_ctx.swapchain_extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    vkCmdBeginRendering(cmd, &rendering);
    set_swapchain_viewport(cmd);
    g_ctx.rendering_active = true;
}

static void end_swapchain_rendering() {
    if(!g_ctx.rendering_active) {
        return;
    }

    const VkCommandBuffer cmd = vk_command_buffer();
    vkCmdEndRendering(cmd);
    g_ctx.rendering_active = false;

    image_barrier(
        cmd,
        g_ctx.swapchain_images[g_ctx.image_index],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        0
    );
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
    const VkPhysicalDeviceFeatures vk10_features{
        .samplerAnisotropy = VK_TRUE,
    };

    const VkDeviceCreateInfo device_ci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vk13_features,
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
    vk_check(vkWaitForFences(g_ctx.device, 1, &frame.submitted, VK_TRUE, UINT64_MAX));
    // Chapter 7 will flush the deletion queue for this frame slot here.
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
    begin_swapchain_rendering();
}

void end_frame() {
    if(!g_ctx.frame_active) {
        return;
    }

    InFlightFrame& frame = g_ctx.frames[g_ctx.frame_index];
    end_swapchain_rendering();
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
        .semaphore = frame.render,
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
        .pWaitSemaphores = &frame.render,
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
        vkDeviceWaitIdle(g_ctx.device);
    }
    destroy_frames();
    destroy_swapchain();
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