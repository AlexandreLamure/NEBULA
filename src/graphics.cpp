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

namespace NEBULA {

// Descriptor sets:
//   Set 0 (frame, persistent): frame UBO, lights SSBO, env cubemap, BRDF LUT
//   Set 1 (pass, per-draw with push descriptors): texture slots 0-3 + storage image
// Texture::bind slots 0-3 → pass set; slots 4-5 → frame set (env / BRDF).

Texture brdf_lut_texture;

struct {
    std::shared_ptr<Texture> black;
    std::shared_ptr<Texture> white;
    std::shared_ptr<Texture> normal;
    std::shared_ptr<Texture> metal_rough;
} default_textures;

bool audit_bindings_before_draw = false;

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

            brdf_program->bind();
            brdf_lut_texture.bind_as_image(0, AccessType::WriteOnly);
            dispatch_compute(brdf_lut_texture.size().x / 8, brdf_lut_texture.size().y / 8, 1);

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


void draw_full_screen_triangle() {
    if(audit_bindings_before_draw) {
        audit_bindings();
    }

    // Fullscreen passes skip Material::bind; keep raster state explicit.
    ctx().cull_mode = VK_CULL_MODE_NONE;
    ctx().depth_test_enable = false;
    ctx().vertex_input = VertexLayout::None;
    if(ctx().bound_program) {
        ctx().bound_program->bind();
        ctx().bound_program->flush_push_constants();
    }

    flush_descriptor_bindings();

    if(!ctx().frame_active) {
        return;
    }

    // No vertex buffer: screen.slang uses SV_VertexID.
    vkCmdDraw(vk_command_buffer(), 3, 1, 0, 0);
}

void blit_to_screen(const Texture& tex) {
    // Expects to be called inside an active swapchain RenderPass.
    ALWAYS_ASSERT(ctx().rendering_active && ctx().rendering_to_swapchain,
                  "blit_to_screen requires an active swapchain RenderPass");

    const std::shared_ptr<Program> blit_program = Program::from_files("screen.slang", "passthrough.slang");
    blit_program->bind();
    tex.bind(0);
    draw_full_screen_triangle();
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





void audit_bindings() {
    ALWAYS_ASSERT(ctx().bound_program, "No pipeline bound (call Program::bind before drawing)");
    ALWAYS_ASSERT(ctx().rendering_active, "No active rendering pass (create a RenderPass first)");

    if(ctx().vertex_input == VertexLayout::Mesh) {
        ALWAYS_ASSERT(ctx().bound_vertex.buffer, "No vertex buffer bound");
        ALWAYS_ASSERT(ctx().bound_index.buffer, "No index buffer bound");
    }

    if(ctx().bound_frame_ubo.buffer) {
        ALWAYS_ASSERT(ctx().bound_frame_ubo.size > 0, "Bound frame UBO has zero size");
    }
    if(ctx().bound_frame_lights.buffer) {
        ALWAYS_ASSERT(ctx().bound_frame_lights.size > 0, "Bound frame lights SSBO has zero size");
    }

    for(const BoundSampledTexture& bound : ctx().bound_textures) {
        if(bound.texture) {
            ALWAYS_ASSERT(bound.texture->vk_view(), "Bound texture has no image view");
        }
    }

    if(ctx().bound_storage_image.texture) {
        ALWAYS_ASSERT(
            ctx().bound_storage_image.texture->vk_storage_view(),
            "Bound storage image has no storage view"
        );
    }
}

}
