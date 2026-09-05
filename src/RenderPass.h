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
        RenderPass(Texture* depth, std::array<Texture*, N> colors, bool clear_depth, bool clear_color, const char* name, Fn&& fn)
            : RenderPass(depth, colors.data(), colors.size(), clear_depth, clear_color, name, std::forward<Fn>(fn)) {
        }

        template<typename Fn>
        RenderPass(Texture* depth, bool clear_depth, const char* name, Fn&& fn)
            : RenderPass(depth, nullptr, 0, clear_depth, false, name, std::forward<Fn>(fn)) {
        }

        template<typename Fn>
        RenderPass(Swapchain, bool clear_color, const char* name, Fn&& fn) {
            begin_swapchain(clear_color);
            {
                PROFILE_GPU(name);
                std::forward<Fn>(fn)();
            }
            end_rendering_if_active();
        }

    private:
        template<typename Fn>
        RenderPass(Texture* depth, Texture* const* colors, size_t count, bool clear_depth, bool clear_color, const char* name, Fn&& fn) {
            begin_offscreen(depth, colors, count, clear_depth, clear_color);
            {
                PROFILE_GPU(name);
                std::forward<Fn>(fn)();
            }
            end_rendering_if_active();
        }

        static void begin_offscreen(Texture* depth, Texture* const* colors, size_t count, bool clear_depth, bool clear_color);
        static void begin_swapchain(bool clear_color);
};

}

#endif // RENDERPASS_H
