#include "Framebuffer.h"

#include "ImageFormat.h"
#include "VkContext.h"

#include <glm/vec4.hpp>

namespace OM3D {

static constexpr glm::vec4 clear_color_value = {0.5f, 0.7f, 0.8f, 1.0f};

static void set_y_flipped_viewport(VkCommandBuffer cmd, glm::uvec2 size) {
    // Vulkan NDC has Y down. Negative height keeps OpenGL-style NDC in shaders.
    const float height = float(size.y);
    const VkViewport viewport{
        .x = 0.0f,
        .y = height,
        .width = float(size.x),
        .height = -height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor{
        .extent = {size.x, size.y},
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

static void transition_for_color_attachment(VkCommandBuffer cmd, Texture& tex) {
    const VkImageLayout old_layout = tex.vk_layout();
    image_barrier(
        cmd,
        tex.vk_image(),
        old_layout,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        old_layout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        old_layout == VK_IMAGE_LAYOUT_UNDEFINED ? VkAccessFlags2(0) : VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    tex.set_vk_layout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

static void transition_for_depth_attachment(VkCommandBuffer cmd, Texture& tex) {
    const VkImageLayout old_layout = tex.vk_layout();
    image_barrier(
        cmd,
        tex.vk_image(),
        old_layout,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        old_layout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        old_layout == VK_IMAGE_LAYOUT_UNDEFINED ? VkAccessFlags2(0) : VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );
    tex.set_vk_layout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
}

Framebuffer::Framebuffer() : _swapchain(true) {
}

Framebuffer::Framebuffer(Texture* depth) : Framebuffer(depth, nullptr, 0) {
}

Framebuffer::Framebuffer(Texture* depth, Texture** colors, size_t count) : _depth(depth) {
    ALWAYS_ASSERT(count <= _colors.size(), "Too many render targets");

    if(depth) {
        _size = depth->size();
    }

    for(size_t i = 0; i != count; ++i) {
        DEBUG_ASSERT(colors[i]);
        _colors[i] = colors[i];
        _size = colors[i]->size();
    }
    _color_count = u32(count);
}

// FIXME: This is a OpenGL-style framebuffer bind.
// I guess for Vulkan + dynamic rendering we should use a "pass" architecture with vkBeginRendering/vkEndRendering enclosure and proper descriptor sets.
void Framebuffer::bind(bool clear_depth, bool clear_color) const {
    ALWAYS_ASSERT(ctx().frame_active, "Framebuffer::bind requires an active frame");

    // Only one vkCmdBeginRendering at a time (same idea as one bound FBO).
    end_rendering_if_active();

    const VkCommandBuffer cmd = vk_command_buffer();
    GraphicsContext& c = ctx();

    if(_swapchain) {
        const glm::uvec2 extent = {c.swapchain_extent.width, c.swapchain_extent.height};
        _size = extent;

        const VkImageLayout old_layout = c.swapchain_layout;
        // First use each frame: layout is UNDEFINED (discard). Later binds (e.g. UI) LOAD.
        const bool first_use = (old_layout == VK_IMAGE_LAYOUT_UNDEFINED);
        image_barrier(
            cmd,
            c.swapchain_images[c.image_index],
            old_layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            first_use ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            first_use ? VkAccessFlags2(0) : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        c.swapchain_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        const VkAttachmentLoadOp color_load =
            clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR
                        : (first_use ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD);

        const VkRenderingAttachmentInfo color{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = c.swapchain_views[c.image_index],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = color_load,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{clear_color_value.r, clear_color_value.g, clear_color_value.b, clear_color_value.a}}},
        };
        const VkRenderingInfo rendering{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = c.swapchain_extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color,
        };

        c.rendering_color_format = c.swapchain_format;
        c.rendering_depth_format = VK_FORMAT_UNDEFINED;
        c.rendering_to_swapchain = true;
        c.rendering_color_count = 0;

        vkCmdBeginRendering(cmd, &rendering);
        set_y_flipped_viewport(cmd, extent);
        c.rendering_active = true;
        return;
    }

    VkRenderingAttachmentInfo color_infos[8] = {};
    for(u32 i = 0; i != _color_count; ++i) {
        Texture* color = _colors[i];
        DEBUG_ASSERT(color && color->_view);
        transition_for_color_attachment(cmd, *color);

        color_infos[i] = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = color->_view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{clear_color_value.r, clear_color_value.g, clear_color_value.b, clear_color_value.a}}},
        };
    }

    VkRenderingAttachmentInfo depth_info{};
    const VkRenderingAttachmentInfo* depth_ptr = nullptr;
    if(_depth) {
        DEBUG_ASSERT(_depth->_view);
        transition_for_depth_attachment(cmd, *_depth);

        depth_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = _depth->_view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            // Reverse-Z: far is 0 (same as the old glClearDepthf(0)).
            .clearValue = {.depthStencil = {.depth = 0.0f}},
        };
        depth_ptr = &depth_info;
    }

    const VkExtent2D extent = {_size.x, _size.y};
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = _color_count,
        .pColorAttachments = _color_count ? color_infos : nullptr,
        .pDepthAttachment = depth_ptr,
    };

    c.rendering_color_format = _color_count
        ? image_format_to_vk(_colors[0]->_format)
        : VK_FORMAT_UNDEFINED;
    c.rendering_depth_format = _depth
        ? image_format_to_vk(_depth->_format)
        : VK_FORMAT_UNDEFINED;
    c.rendering_to_swapchain = false;
    c.rendering_color_count = _color_count;
    for(u32 i = 0; i != _color_count; ++i) {
        c.rendering_colors[i] = _colors[i];
    }

    vkCmdBeginRendering(cmd, &rendering);
    set_y_flipped_viewport(cmd, _size);
    c.rendering_active = true;
}

const glm::uvec2& Framebuffer::size() const {
    return _size;
}

}
