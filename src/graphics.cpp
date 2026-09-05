#include "VkContext.h"
#include "graphics.h"

#include "RenderPass.h"
#include "Texture.h"
#include "TimestampQuery.h"
#include "Program.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <vector>
#include <cstring>

namespace nebula {

// Descriptor sets:
//   Set 0 (frame, persistent): frame UBO, lights SSBO, env cubemap, BRDF LUT
//   Set 1 (pass, per-draw with push descriptors): texture slots 0-3 + storage image

Texture brdfLutTexture;

struct {
    std::shared_ptr<Texture> black;
    std::shared_ptr<Texture> white;
    std::shared_ptr<Texture> normal;
    std::shared_ptr<Texture> metalRough;
} defaultTextures;

void initGraphics(GLFWwindow* window) {
    vkInit(window);

    {
        // Split-sum IBL: a 256² RG LUT of the BRDF scale/bias terms. Written once
        // with a compute shader via immediateSubmit before the first frame.
        brdfLutTexture = Texture(glm::uvec2(256), ImageFormat::RG16_UNORM, WrapMode::Clamp);

        std::shared_ptr<Program> brdfProgram = Program::fromFile("brdf.slang");
        DEBUG_ASSERT(brdfProgram && brdfProgram->isCompute());

        immediateSubmit([&](VkCommandBuffer cmd) {
            imageBarrier(
                cmd,
                brdfLutTexture.vkImage(),
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                0,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT
            );
            brdfLutTexture.setVkLayout(VK_IMAGE_LAYOUT_GENERAL);

            PassResources pass{};
            pass.storageImage = &brdfLutTexture;
            dispatch(*brdfProgram, pass, brdfLutTexture.size().x / 8, brdfLutTexture.size().y / 8, 1);

            imageBarrier(
                cmd,
                brdfLutTexture.vkImage(),
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT
            );
            brdfLutTexture.setVkLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
    }

    {
        TextureData data;
        data.format = ImageFormat::RGBA8_UNORM;
        data.size = glm::uvec2(2, 2);
        data.data = std::make_unique<u8[]>(16);

        {
            std::memset(data.data.get(), 0, 16);
            defaultTextures.black = std::make_shared<Texture>(data);
        }
        {
            std::memset(data.data.get(), 255, 16);
            defaultTextures.white = std::make_shared<Texture>(data);
        }
        {
            std::memset(data.data.get(), 0, 16);
            for(size_t i = 0; i != 4; ++i) {
                data.data[i * 4 + 0] = 127;
                data.data[i * 4 + 1] = 127;
                data.data[i * 4 + 2] = 255;
            }
            defaultTextures.normal = std::make_shared<Texture>(data);
        }
        {
            std::memset(data.data.get(), 0, 16);
            for(size_t i = 0; i != 4; ++i) {
                data.data[i * 4 + 1] = u8(255.0f * 0.6f);
                data.data[i * 4 + 2] = 0;
            }
            defaultTextures.metalRough = std::make_shared<Texture>(data);
        }
    }
}

void destroyGraphics() {
    brdfLutTexture = {};
    defaultTextures = {};
    profile::destroyProfile();

    vkDestroy();
}

const Texture& brdfLut() {
    return brdfLutTexture;
}

void drawMesh(
    const Program& program,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push,
    VkBuffer vbo,
    VkBuffer ibo,
    u32 indexCount
) {
    drawIndexed(
        program,
        VertexLayout::Mesh,
        raster,
        pass,
        push,
        vbo,
        ibo,
        indexCount,
        0,
        0,
        VK_INDEX_TYPE_UINT32
    );
}

void drawFullscreen(
    const Program& program,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push
) {
    ALWAYS_ASSERT(ctx().renderingActive, "No active rendering pass");

    program.bindGraphics(raster, VertexLayout::None, push);
    pushPassDescriptors(pass, false);

    if(!ctx().frameActive && !ctx().immediateCmd) {
        return;
    }

    // No vertex buffer: screen.slang uses SV_VertexID.
    vkCmdDraw(vkCommandBuffer(), 3, 1, 0, 0);
}

void drawIndexed(
    const Program& program,
    VertexLayout layout,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push,
    VkBuffer vbo,
    VkBuffer ibo,
    u32 indexCount,
    u32 firstIndex,
    i32 vertexOffset,
    VkIndexType indexType
) {
    ALWAYS_ASSERT(ctx().renderingActive, "No active rendering pass");
    ALWAYS_ASSERT(vbo && ibo, "Vertex/index buffers required");

    program.bindGraphics(raster, layout, push);
    pushPassDescriptors(pass, false);

    if(!ctx().frameActive && !ctx().immediateCmd) {
        return;
    }

    const VkCommandBuffer cmd = vkCommandBuffer();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);
    vkCmdBindIndexBuffer(cmd, ibo, 0, indexType);
    vkCmdDrawIndexed(cmd, indexCount, 1, firstIndex, vertexOffset, 0);
}

void dispatch(const Program& program, const PassResources& pass, u32 x, u32 y, u32 z) {
    if(!vkIsRecording()) {
        return;
    }
    program.bindCompute();
    pushPassDescriptors(pass, true);
    vkCmdDispatch(vkCommandBuffer(), x, y, z);
}

void blitToScreen(const Texture& tex) {
    // Expects to be called inside an active swapchain RenderPass.
    ALWAYS_ASSERT(ctx().renderingActive && ctx().renderingToSwapchain,
                  "blitToScreen requires an active swapchain RenderPass");

    const std::shared_ptr<Program> blitProgram = Program::fromFiles("screen.slang", "passthrough.slang");
    PassResources pass{};
    pass.textures[0] = &tex;
    const RasterState raster{
        .depthTestEnable = false,
        .cullMode = VK_CULL_MODE_NONE,
    };
    drawFullscreen(*blitProgram, raster, pass, {});
}

std::shared_ptr<Texture> defaultBlackTexture() {
    return defaultTextures.black;
}

std::shared_ptr<Texture> defaultWhiteTexture() {
    return defaultTextures.white;
}

std::shared_ptr<Texture> defaultNormalTexture() {
    return defaultTextures.normal;
}

std::shared_ptr<Texture> defaultMetalRoughTexture() {
    return defaultTextures.metalRough;
}

}
