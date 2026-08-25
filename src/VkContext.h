#ifndef VKCONTEXT_H
#define VKCONTEXT_H

#include <utils.h>

#include <volk.h>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_HEADERS_ALREADY_INCLUDED
#include <vk_mem_alloc.h>

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace OM3D {

class Program;

static constexpr u32 frames_in_flight = 2;
static constexpr u32 descriptor_binding_count = 9;

struct InFlightFrame {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkSemaphore render = VK_NULL_HANDLE;
};

// Sticky GL-style bind points. Chapter 9 will flush these into a descriptor set at draw time.
struct BoundBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

// GPU work is often one frame behind the CPU. Destroying a VkBuffer at the end of
// Scene::render() is safe only if we wait for that frame's fence first.
struct DeletionEntry {
    enum class Type : u32 {
        Buffer,
        Image,
        ImageView,
        Sampler,
        Pipeline,
        ShaderModule,
    };
    Type type = Type::Buffer;
    VmaAllocation allocation = nullptr;
    union {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImage image;
        VkImageView image_view;
        VkSampler sampler;
        VkPipeline pipeline;
        VkShaderModule shader_module;
    };
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
    VkCommandPool immediate_pool = VK_NULL_HANDLE;

    std::string device_name;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    const Program* bound_program = nullptr;
    BoundBuffer bound_vertex;
    BoundBuffer bound_index;
    BoundBuffer bound_descriptors[descriptor_binding_count] = {};
    bool alpha_blend = false;
    bool has_vertex_input = true;
    VkFormat rendering_color_format = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat rendering_depth_format = VK_FORMAT_UNDEFINED;

    std::vector<DeletionEntry> deletions[frames_in_flight];

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

// One-shot command buffer: record, submit, wait. Used for staging uploads (and later compute init).
void immediate_submit(std::function<void(VkCommandBuffer)>&& record);

// Enqueue Vulkan objects tagged with the in-flight frame that may still be using them.
// Texture (Chapter 10) will use the image/view/sampler overloads.
void defer_destroy(VkBuffer buffer, VmaAllocation allocation);
void defer_destroy(VkImage image, VmaAllocation allocation);
void defer_destroy(VkImageView view);
void defer_destroy(VkSampler sampler);
void defer_destroy(VkPipeline pipeline);
void defer_destroy(VkShaderModule module);

void flush_frame_deletions(u32 frame_slot);
void flush_all_deletions();

}

#endif // VKCONTEXT_H
