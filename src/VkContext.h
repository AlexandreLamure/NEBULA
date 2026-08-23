#ifndef VKCONTEXT_H
#define VKCONTEXT_H

#include <utils.h>

#include <volk.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_HEADERS_ALREADY_INCLUDED
#include <vk_mem_alloc.h>

#include <string>
#include <vector>

struct GLFWwindow;

namespace OM3D {

static constexpr u32 frames_in_flight = 2;

struct InFlightFrame {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkSemaphore render = VK_NULL_HANDLE;
};

// Singleton containing the Vulkan context.
struct GraphicsContext {
    GLFWwindow* window = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    u32 graphics_queue_family = 0;
    VkQueue graphics_queue = VK_NULL_HANDLE;

    VmaAllocator allocator = VK_NULL_HANDLE;

    std::string device_name;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchain_format = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D swapchain_extent = {};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_views;

    InFlightFrame frames[frames_in_flight] = {};
    u32 frame_index = 0;
    u32 image_index = 0;
    bool frame_active = false;
    bool rendering_active = false;
};
GraphicsContext& ctx();

void vk_init(GLFWwindow* window);
void vk_destroy();

inline VkInstance vk_instance() { return ctx().instance; }
inline VkPhysicalDevice vk_physical_device() { return ctx().physical_device; }
inline VkDevice vk_device() { return ctx().device; }
inline VkQueue vk_queue() { return ctx().graphics_queue; }
inline u32 vk_queue_family() { return ctx().graphics_queue_family; }
inline VkSurfaceKHR vk_surface() { return ctx().surface; }
inline VmaAllocator device_allocator() { return ctx().allocator; }
inline const std::string& device_name() { return ctx().device_name; }
inline VkCommandBuffer vk_command_buffer() { return ctx().frames[ctx().frame_index].command_buffer; }
inline u32 vk_frame_index() { return ctx().frame_index; }

inline void vk_check(VkResult result) {
    ALWAYS_ASSERT(result == VK_SUCCESS, "Vulkan call failed");
}

}

#endif // VKCONTEXT_H
