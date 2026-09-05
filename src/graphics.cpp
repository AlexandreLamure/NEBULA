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

Texture brdf_lut_texture;

struct {
    std::shared_ptr<Texture> black;
    std::shared_ptr<Texture> white;
    std::shared_ptr<Texture> normal;
    std::shared_ptr<Texture> metal_rough;
} default_textures;

void init_graphics(GLFWwindow* window) {
    vk_init(window);

    {
        // Split-sum IBL: a 256² RG LUT of the BRDF scale/bias terms. Written once
        // with a compute shader via immediate_submit before the first frame.
        brdf_lut_texture = Texture(glm::uvec2(256), ImageFormat::RG16_UNORM, WrapMode::Clamp);

        std::shared_ptr<Program> brdf_program = Program::from_file("brdf.slang");
        DEBUG_ASSERT(brdf_program && brdf_program->is_compute());

        immediate_submit([&](VkCommandBuffer cmd) {
            image_barrier(
                cmd,
                brdf_lut_texture.vk_image(),
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                0,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT
            );
            brdf_lut_texture.set_vk_layout(VK_IMAGE_LAYOUT_GENERAL);

            PassResources pass{};
            pass.storage_image = &brdf_lut_texture;
            dispatch(*brdf_program, pass, brdf_lut_texture.size().x / 8, brdf_lut_texture.size().y / 8, 1);

            image_barrier(
                cmd,
                brdf_lut_texture.vk_image(),
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT
            );
            brdf_lut_texture.set_vk_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
    }

    {
        TextureData data;
        data.format = ImageFormat::RGBA8_UNORM;
        data.size = glm::uvec2(2, 2);
        data.data = std::make_unique<u8[]>(16);

        {
            std::memset(data.data.get(), 0, 16);
            default_textures.black = std::make_shared<Texture>(data);
        }
        {
            std::memset(data.data.get(), 255, 16);
            default_textures.white = std::make_shared<Texture>(data);
        }
        {
            std::memset(data.data.get(), 0, 16);
            for(size_t i = 0; i != 4; ++i) {
                data.data[i * 4 + 0] = 127;
                data.data[i * 4 + 1] = 127;
                data.data[i * 4 + 2] = 255;
            }
            default_textures.normal = std::make_shared<Texture>(data);
        }
        {
            std::memset(data.data.get(), 0, 16);
            for(size_t i = 0; i != 4; ++i) {
                data.data[i * 4 + 1] = u8(255.0f * 0.6f);
                data.data[i * 4 + 2] = 0;
            }
            default_textures.metal_rough = std::make_shared<Texture>(data);
        }
    }
}

void destroy_graphics() {
    brdf_lut_texture = {};
    default_textures = {};
    profile::destroy_profile();

    vk_destroy();
}

const Texture& brdf_lut() {
    return brdf_lut_texture;
}

void draw_mesh(
    const Program& program,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push,
    VkBuffer vbo,
    VkBuffer ibo,
    u32 index_count
) {
    draw_indexed(
        program,
        VertexLayout::Mesh,
        raster,
        pass,
        push,
        vbo,
        ibo,
        index_count,
        0,
        0,
        VK_INDEX_TYPE_UINT32
    );
}

void draw_fullscreen(
    const Program& program,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push
) {
    ALWAYS_ASSERT(ctx().rendering_active, "No active rendering pass");

    program.bind_graphics(raster, VertexLayout::None, push);
    push_pass_descriptors(pass, false);

    if(!ctx().frame_active && !ctx().immediate_cmd) {
        return;
    }

    // No vertex buffer: screen.slang uses SV_VertexID.
    vkCmdDraw(vk_command_buffer(), 3, 1, 0, 0);
}

void draw_indexed(
    const Program& program,
    VertexLayout layout,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push,
    VkBuffer vbo,
    VkBuffer ibo,
    u32 index_count,
    u32 first_index,
    i32 vertex_offset,
    VkIndexType index_type
) {
    ALWAYS_ASSERT(ctx().rendering_active, "No active rendering pass");
    ALWAYS_ASSERT(vbo && ibo, "Vertex/index buffers required");

    program.bind_graphics(raster, layout, push);
    push_pass_descriptors(pass, false);

    if(!ctx().frame_active && !ctx().immediate_cmd) {
        return;
    }

    const VkCommandBuffer cmd = vk_command_buffer();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);
    vkCmdBindIndexBuffer(cmd, ibo, 0, index_type);
    vkCmdDrawIndexed(cmd, index_count, 1, first_index, vertex_offset, 0);
}

void dispatch(const Program& program, const PassResources& pass, u32 x, u32 y, u32 z) {
    if(!vk_is_recording()) {
        return;
    }
    program.bind_compute();
    push_pass_descriptors(pass, true);
    vkCmdDispatch(vk_command_buffer(), x, y, z);
}

void blit_to_screen(const Texture& tex) {
    // Expects to be called inside an active swapchain RenderPass.
    ALWAYS_ASSERT(ctx().rendering_active && ctx().rendering_to_swapchain,
                  "blit_to_screen requires an active swapchain RenderPass");

    const std::shared_ptr<Program> blit_program = Program::from_files("screen.slang", "passthrough.slang");
    PassResources pass{};
    pass.textures[0] = &tex;
    const RasterState raster{
        .depth_test_enable = false,
        .cull_mode = VK_CULL_MODE_NONE,
    };
    draw_fullscreen(*blit_program, raster, pass, {});
}

std::shared_ptr<Texture> default_black_texture() {
    return default_textures.black;
}

std::shared_ptr<Texture> default_white_texture() {
    return default_textures.white;
}

std::shared_ptr<Texture> default_normal_texture() {
    return default_textures.normal;
}

std::shared_ptr<Texture> default_metal_rough_texture() {
    return default_textures.metal_rough;
}

}
