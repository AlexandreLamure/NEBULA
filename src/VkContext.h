#ifndef VKCONTEXT_H
#define VKCONTEXT_H

#include <utils.h>

#include <volk.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_HEADERS_ALREADY_INCLUDED
#include <vk_mem_alloc.h>

#include <string>

struct GLFWwindow;

namespace OM3D {

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

inline void vk_check(VkResult result) {
    ALWAYS_ASSERT(result == VK_SUCCESS, "Vulkan call failed");
}

}

#endif // VKCONTEXT_H
