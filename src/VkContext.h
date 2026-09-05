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

namespace nebula {

class Program;
class Texture;

static constexpr u32 framesInFlight = 2;

// Set 0 — frame (persistent, updated once per Scene::render):
//   0: frame UBO, 1: lights SSBO, 2: env cubemap, 3: BRDF LUT
static constexpr u32 frameSet = 0;
static constexpr u32 frameBindingCount = 4;
static constexpr u32 frameUboBinding = 0;
static constexpr u32 frameLightsBinding = 1;
static constexpr u32 frameEnvBinding = 2;
static constexpr u32 frameBrdfBinding = 3;

// Set 1 — pass (pushed each draw/dispatch):
//   0-3: sampled textures, 4: storage image
static constexpr u32 passSet = 1;
static constexpr u32 passTextureSlotCount = 4;
static constexpr u32 passStorageBinding = 4;
static constexpr u32 passBindingCount = 5;

// Two timestamps per PROFILE_GPU zone (begin + end).
static constexpr u32 timestampQueriesPerFrame = 1024;

// Vertex input is pipeline state in Vulkan (OpenGL set it per draw with glVertexAttribPointer).
enum class VertexLayout : u32 {
    None = 0,  // Fullscreen: SV_VertexID, no vertex buffer
    Mesh = 1,  // Vertex.h locations 0–4
    ImGui = 2, // ImDrawVert: float2 pos, float2 uv, RGBA8 unorm color
};

struct InFlightFrame {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    VkSemaphore acquire = VK_NULL_HANDLE;
    VkDescriptorSet frameDescriptorSet = VK_NULL_HANDLE;
};

// Explicit per-draw raster intent (depth/cull are dynamic; blend selects a pipeline variant).
struct RasterState {
    bool alphaBlend = false;
    bool depthTestEnable = true;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
};

// Set 1 resources pushed each draw/dispatch.
struct PassResources {
    const Texture* textures[passTextureSlotCount] = {};
    const Texture* storageImage = nullptr;
};

// Set 0 resources updated once per Scene::render.
struct FrameResources {
    VkBuffer ubo = VK_NULL_HANDLE;
    VkDeviceSize uboSize = 0;
    VkBuffer lights = VK_NULL_HANDLE;
    VkDeviceSize lightsSize = 0;
    const Texture* env = nullptr;
    const Texture* brdf = nullptr;
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
        VkImageView imageView;
        VkSampler sampler;
        VkPipeline pipeline;
        VkShaderModule shaderModule;
    };
};

// Singleton containing the Vulkan context.
struct GraphicsContext {
    GLFWwindow* window = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    u32 graphicsQueueFamily = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;

    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool immediatePool = VK_NULL_HANDLE;
    // Non-null while immediateSubmit is recording; vkCommandBuffer() prefers this.
    VkCommandBuffer immediateCmd = VK_NULL_HANDLE;

    std::string deviceName;

    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout passSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool frameDescriptorPool = VK_NULL_HANDLE;

    VkSampler samplerRepeat = VK_NULL_HANDLE;
    VkSampler samplerClamp = VK_NULL_HANDLE;
    VkImage fallbackSampledImage = VK_NULL_HANDLE;
    VmaAllocation fallbackSampledAllocation = nullptr;
    VkImageView fallbackSampledView = VK_NULL_HANDLE;

    // Attachment formats for the active RenderPass (pipeline variant key).
    VkFormat renderingColorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat renderingDepthFormat = VK_FORMAT_UNDEFINED;

    std::vector<DeletionEntry> deletions[framesInFlight];

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D swapchainExtent = {};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    // One render-complete semaphore per swapchain image. Reusing a per-frame
    // semaphore is invalid when imageCount > framesInFlight: present may still
    // be waiting on it for a different image index.
    std::vector<VkSemaphore> swapchainRenderSemaphores;

    InFlightFrame frames[framesInFlight] = {};
    VkQueryPool timestampPools[framesInFlight] = {};
    u32 timestampAllocated[framesInFlight] = {};
    float timestampPeriod = 1.0f;
    u32 timestampValidBits = 0;
    u32 frameIndex = 0;
    u32 imageIndex = 0;
    bool frameActive = false;
    bool renderingActive = false;
    bool renderingToSwapchain = false;
    // Offscreen color attachments of the current vkCmdBeginRendering. After
    // the RenderPass ends they become SHADER_READ_ONLY so the next pass can
    // sample them. Swapchain is not a Texture.
    Texture* renderingColors[8] = {};
    u32 renderingColorCount = 0;
    // Per-frame: UNDEFINED until a swapchain RenderPass begins; PRESENT after endFrame.
    VkImageLayout swapchainLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};
GraphicsContext& ctx();

void vkInit(GLFWwindow* window);
void vkDestroy();

// Wait until all submitted GPU work finishes. Required before recreating swapchain
// images or offscreen attachments the GPU may still be reading/writing.
void waitForGpuIdle();

inline VkInstance vkInstance() { return ctx().instance; }
inline VkPhysicalDevice vkPhysicalDevice() { return ctx().physicalDevice; }
inline VkDevice vkDevice() { return ctx().device; }
inline VkQueue vkQueue() { return ctx().graphicsQueue; }
inline u32 vkQueueFamily() { return ctx().graphicsQueueFamily; }
inline VkSurfaceKHR vkSurface() { return ctx().surface; }
inline VmaAllocator deviceAllocator() { return ctx().allocator; }
inline const std::string& deviceName() { return ctx().deviceName; }
inline VkCommandBuffer vkCommandBuffer() {
    if(ctx().immediateCmd) {
        return ctx().immediateCmd;
    }
    return ctx().frames[ctx().frameIndex].commandBuffer;
}
inline bool vkIsRecording() {
    return ctx().immediateCmd || ctx().frameActive;
}
inline u32 vkFrameIndex() { return ctx().frameIndex; }

void vkCheckImpl(VkResult result, const char* call, const char* file, int line);
#define vkCheck(call) ::nebula::vkCheckImpl((call), #call, __FILE__, __LINE__)

// One-shot command buffer: record, submit, wait. Used for staging uploads and
// init-time compute (BRDF LUT, env cubemap) so the result exists before the first frame.
void immediateSubmit(std::function<void(VkCommandBuffer)>&& record);

void imageBarrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT
);

// Ends the current vkCmdBeginRendering if any. Offscreen color attachments
// become SHADER_READ_ONLY so the next pass can sample them. Does not transition
// the swapchain to PRESENT (endFrame does that after ImGui has drawn).
void endRenderingIfActive();

// Enqueue Vulkan objects tagged with the in-flight frame that may still be using them.
// Texture (Chapter 10) will use the image/view/sampler overloads.
void deferDestroy(VkBuffer buffer, VmaAllocation allocation);
void deferDestroy(VkImage image, VmaAllocation allocation);
void deferDestroy(VkImageView view);
void deferDestroy(VkSampler sampler);
void deferDestroy(VkPipeline pipeline);
void deferDestroy(VkShaderModule module);

void flushFrameDeletions(u32 frameSlot);
void flushAllDeletions();

// Update persistent set 0 from FrameResources and bind it (graphics bind point).
void bindFrame(const FrameResources& frame);

// Push set 1 from PassResources for the next draw/dispatch.
void pushPassDescriptors(const PassResources& pass, bool compute);

// Fence for this frame slot has been waited: copy timestamps into queued PROFILE_GPU
// markers, then vkResetQueryPool so the slot can be recorded again.
void resetTimestampQueries();

}

#endif // VKCONTEXT_H
