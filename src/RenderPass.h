#ifndef RENDERPASS_H
#define RENDERPASS_H

#include <Texture.h>
#include <TimestampQuery.h>

#include <array>
#include <utility>

namespace nebula {

// Runs fn inside vkCmdBeginRendering / vkCmdEndRendering (Vulkan 1.3 dynamic rendering).
// Ending the pass transitions offscreen color attachments to SHADER_READ_ONLY so the
// next pass can sample them.
class RenderPass : NonCopyable {
    public:
        struct Swapchain {};

        template<size_t N, typename Fn>
        RenderPass(Texture* depth, std::array<Texture*, N> colors, bool clearDepth, bool clearColor, const char* name, Fn&& fn)
            : RenderPass(depth, colors.data(), colors.size(), clearDepth, clearColor, name, std::forward<Fn>(fn)) {
        }

        template<typename Fn>
        RenderPass(Texture* depth, bool clearDepth, const char* name, Fn&& fn)
            : RenderPass(depth, nullptr, 0, clearDepth, false, name, std::forward<Fn>(fn)) {
        }

        template<typename Fn>
        RenderPass(Swapchain, bool clearColor, const char* name, Fn&& fn) {
            beginSwapchain(clearColor);
            {
                PROFILE_GPU(name);
                std::forward<Fn>(fn)();
            }
            endRenderingIfActive();
        }

    private:
        template<typename Fn>
        RenderPass(Texture* depth, Texture* const* colors, size_t count, bool clearDepth, bool clearColor, const char* name, Fn&& fn) {
            beginOffscreen(depth, colors, count, clearDepth, clearColor);
            {
                PROFILE_GPU(name);
                std::forward<Fn>(fn)();
            }
            endRenderingIfActive();
        }

        static void beginOffscreen(Texture* depth, Texture* const* colors, size_t count, bool clearDepth, bool clearColor);
        static void beginSwapchain(bool clearColor);
};

}

#endif // RENDERPASS_H
