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
class Texture;

static constexpr u32 frames_in_flight = 2;
static constexpr u32 descriptor_binding_count = 9;
// OpenGL texture units 0-5 map to descriptor bindings 2-7 (see graphics.cpp).
static constexpr u32 gl_texture_slot_count = 6;
static constexpr u32 descriptor_texture_binding_base = 2;
// Two timestamps per PROFILE_GPU zone (begin + end).
static constexpr u32 timestamp_queries_per_frame = 1024;

// Vertex input is pipeline state in Vulkan (OpenGL set it per draw with glVertexAttribPointer).
enum class VertexLayout : u32 {
    None = 0,  // Fullscreen: SV_VertexID, no vertex buffer
    Mesh = 1,  // Vertex.h locations 0–4
    ImGui = 2, // ImDrawVert: float2 pos, float2 uv, RGBA8 unorm color
};

struct InFlightFrame {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkSemaphore render = VK_NULL_HANDLE;
};

// Sticky GL-style bind points, flushed into one descriptor set at draw/dispatch time.
struct BoundBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

struct BoundSampledTexture {
    const Texture* texture = nullptr;
};

struct BoundStorageImage {
    const Texture* texture = nullptr;
    VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;
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
    // Non-null while immediate_submit is recording; vk_command_buffer() prefers this.
    VkCommandBuffer immediate_cmd = VK_NULL_HANDLE;

    std::string device_name;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

    VkSampler sampler_repeat = VK_NULL_HANDLE;
    VkSampler sampler_clamp = VK_NULL_HANDLE;
    VkImage fallback_sampled_image = VK_NULL_HANDLE;
    VmaAllocation fallback_sampled_allocation = nullptr;
    VkImageView fallback_sampled_view = VK_NULL_HANDLE;

    // FIXME: ugly state machine like OpenGL.
    const Program* bound_program = nullptr;
    BoundBuffer bound_vertex;
    BoundBuffer bound_index;
    BoundBuffer bound_descriptors[descriptor_binding_count] = {};
    BoundSampledTexture bound_textures[gl_texture_slot_count] = {};
    BoundStorageImage bound_storage_image;
    // Raster state written by Material::bind(), applied after vkCmdBindPipeline
    // (blend is baked into the pipeline; depth/cull are Vulkan 1.3 dynamic state).
    bool alpha_blend = false;
    bool depth_test_enable = true;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_GREATER_OR_EQUAL;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VertexLayout vertex_input = VertexLayout::Mesh;
    VkFormat rendering_color_format = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat rendering_depth_format = VK_FORMAT_UNDEFINED;

    std::vector<DeletionEntry> deletions[frames_in_flight];

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchain_format = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D swapchain_extent = {};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_views;

    InFlightFrame frames[frames_in_flight] = {};
    VkQueryPool timestamp_pools[frames_in_flight] = {};
    u32 timestamp_allocated[frames_in_flight] = {};
    float timestamp_period = 1.0f;
    u32 timestamp_valid_bits = 0;
    u32 frame_index = 0;
    u32 image_index = 0;
    bool frame_active = false;
    bool rendering_active = false;
    bool rendering_to_swapchain = false;
    // Offscreen color attachments of the current vkCmdBeginRendering. After
    // vkCmdEndRendering they become SHADER_READ_ONLY so the next pass can sample
    // them (GL: unbind the FBO, then bind its texture). Swapchain is not a Texture.
    Texture* rendering_colors[8] = {};
    u32 rendering_color_count = 0;
    // Per-frame: UNDEFINED until Framebuffer binds the swapchain; PRESENT after end_frame.
    VkImageLayout swapchain_layout = VK_IMAGE_LAYOUT_UNDEFINED;
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
inline VkCommandBuffer vk_command_buffer() {
    if(ctx().immediate_cmd) {
        return ctx().immediate_cmd;
    }
    return ctx().frames[ctx().frame_index].command_buffer;
}
inline bool vk_is_recording() {
    return ctx().immediate_cmd || ctx().frame_active;
}
inline u32 vk_frame_index() { return ctx().frame_index; }

inline void vk_check(VkResult result) {
    ALWAYS_ASSERT(result == VK_SUCCESS, "Vulkan call failed");
}

// One-shot command buffer: record, submit, wait. Used for staging uploads and
// init-time compute (BRDF LUT, env cubemap) so the result exists before the first frame.
void immediate_submit(std::function<void(VkCommandBuffer)>&& record);

void image_barrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT
);

// Ends the current vkCmdBeginRendering if any. Offscreen color attachments
// become SHADER_READ_ONLY so the next pass can sample them. Does not transition
// the swapchain to PRESENT (end_frame does that after ImGui has drawn).
void end_rendering_if_active();

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

// Allocate one descriptor set from the per-frame pool, write bindings, vkCmdBindDescriptorSets.
void flush_descriptor_bindings();

void dispatch_compute(u32 x, u32 y, u32 z);

// Fence for this frame slot has been waited: copy timestamps into queued PROFILE_GPU
// markers, then vkResetQueryPool so the slot can be recorded again.
void reset_timestamp_queries();

}

#endif // VKCONTEXT_H
