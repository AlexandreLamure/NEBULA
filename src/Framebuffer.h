#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <Texture.h>

#include <array>

namespace OM3D {

// Not a VkFramebuffer. Stores the attachments we are currently drawing into;
// bind() drives vkCmdBeginRendering (Vulkan 1.3 dynamic rendering).
// TODO: maybe remove that class
class Framebuffer : NonCopyable {
    public:
        template<size_t N>
        Framebuffer(Texture* depth, std::array<Texture*, N> colors) : Framebuffer(depth, colors.data(), colors.size()) {
        }


        // Default framebuffer = current swapchain image.
        Framebuffer();
        Framebuffer(Texture* depth);

        Framebuffer(Framebuffer&&) = default;
        Framebuffer& operator=(Framebuffer&&) = default;

        ~Framebuffer() = default;

        void bind(bool clear_depth, bool clear_color) const;

        const glm::uvec2& size() const;

    private:
        Framebuffer(Texture* depth, Texture** colors, size_t count);

        Texture* _depth = nullptr;
        std::array<Texture*, 8> _colors = {};
        u32 _color_count = 0;
        mutable glm::uvec2 _size = {};
        bool _swapchain = false;
};

}

#endif // FRAMEBUFFER_H
