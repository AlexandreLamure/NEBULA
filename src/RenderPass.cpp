#include "RenderPass.h"

#include "VkContext.h"

#include <glm/vec4.hpp>

namespace nebula {

static constexpr glm::vec4 clearColorValue = {0.5f, 0.7f, 0.8f, 1.0f};

static void setYFlippedViewport(VkCommandBuffer cmd, glm::uvec2 size) {
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

static void transitionForColorAttachment(VkCommandBuffer cmd, Texture& tex) {
    const VkImageLayout oldLayout = tex.vkLayout();
    imageBarrier(
        cmd,
        tex.vkImage(),
        oldLayout,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VkAccessFlags2(0) : VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    tex.setVkLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

static void transitionForDepthAttachment(VkCommandBuffer cmd, Texture& tex) {
    const VkImageLayout oldLayout = tex.vkLayout();
    imageBarrier(
        cmd,
        tex.vkImage(),
        oldLayout,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VkAccessFlags2(0) : VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );
    tex.setVkLayout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
}

void RenderPass::beginSwapchain(bool clearColor) {
    ALWAYS_ASSERT(ctx().frameActive, "RenderPass requires an active frame");
    ALWAYS_ASSERT(!ctx().renderingActive, "A RenderPass is already active");

    GraphicsContext& c = ctx();
    const VkCommandBuffer cmd = vkCommandBuffer();

    const glm::uvec2 extent = {c.swapchainExtent.width, c.swapchainExtent.height};

    const VkImageLayout oldLayout = c.swapchainLayout;
        // First use each frame: layout is UNDEFINED (discard). A later swapchain pass LOADs.
    const bool firstUse = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
    imageBarrier(
        cmd,
        c.swapchainImages[c.imageIndex],
        oldLayout,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        firstUse ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        firstUse ? VkAccessFlags2(0) : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    c.swapchainLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    const VkAttachmentLoadOp colorLoad =
        clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR
                    : (firstUse ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD);

    const VkRenderingAttachmentInfo color{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = c.swapchainViews[c.imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = colorLoad,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{clearColorValue.r, clearColorValue.g, clearColorValue.b, clearColorValue.a}}},
    };
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = c.swapchainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };

    c.renderingColorFormat = c.swapchainFormat;
    c.renderingDepthFormat = VK_FORMAT_UNDEFINED;
    c.renderingToSwapchain = true;
    c.renderingColorCount = 0;

    vkCmdBeginRendering(cmd, &rendering);
    setYFlippedViewport(cmd, extent);

    c.renderingActive = true;
}

void RenderPass::beginOffscreen(Texture* depth, Texture* const* colors, size_t count, bool clearDepth, bool clearColor) {
    ALWAYS_ASSERT(ctx().frameActive, "RenderPass requires an active frame");
    ALWAYS_ASSERT(!ctx().renderingActive, "A RenderPass is already active");
    ALWAYS_ASSERT(count <= 8, "Too many render targets");

    GraphicsContext& c = ctx();
    const VkCommandBuffer cmd = vkCommandBuffer();

    glm::uvec2 size = {};
    if(depth) {
        size = depth->size();
    }
    for(size_t i = 0; i != count; ++i) {
        DEBUG_ASSERT(colors[i]);
        size = colors[i]->size();
    }

    VkRenderingAttachmentInfo colorInfos[8] = {};
    for(u32 i = 0; i != u32(count); ++i) {
        Texture* color = colors[i];
        DEBUG_ASSERT(color && color->vkView());
        transitionForColorAttachment(cmd, *color);

        colorInfos[i] = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = color->vkView(),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {{clearColorValue.r, clearColorValue.g, clearColorValue.b, clearColorValue.a}}},
        };
    }

    VkRenderingAttachmentInfo depthInfo{};
    const VkRenderingAttachmentInfo* depthPtr = nullptr;
    if(depth) {
        DEBUG_ASSERT(depth->vkView());
        transitionForDepthAttachment(cmd, *depth);

        depthInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depth->vkView(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            // Reverse-Z: far is 0 (same as the old glClearDepthf(0)).
            .clearValue = {.depthStencil = {.depth = 0.0f}},
        };
        depthPtr = &depthInfo;
    }

    const VkExtent2D extent = {size.x, size.y};
    const VkRenderingInfo rendering{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = u32(count),
        .pColorAttachments = count ? colorInfos : nullptr,
        .pDepthAttachment = depthPtr,
    };

    c.renderingColorFormat = count ? colors[0]->vkFormat() : VK_FORMAT_UNDEFINED;
    c.renderingDepthFormat = depth ? depth->vkFormat() : VK_FORMAT_UNDEFINED;
    c.renderingToSwapchain = false;
    c.renderingColorCount = u32(count);
    for(u32 i = 0; i != u32(count); ++i) {
        c.renderingColors[i] = colors[i];
    }

    vkCmdBeginRendering(cmd, &rendering);
    setYFlippedViewport(cmd, size);

    c.renderingActive = true;
}

}
