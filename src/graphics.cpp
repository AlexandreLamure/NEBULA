#include "VkContext.h"
#include "graphics.h"

#include "Framebuffer.h"
#include "Texture.h"
#include "TimestampQuery.h"
#include "Program.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <cstring>

namespace OM3D {

// TODO: maybe make a descriptor set per pass type
// Descriptor set 0 — one layout for the whole engine (GL bind emulator):
//   0: frame UBO (ByteBuffer::bind Uniform, 0)
//   1: point-light SSBO (ByteBuffer::bind Storage, 1)
//   2-5: material textures (Texture::bind GL slots 0-3)
//   6-7: env cubemap / BRDF LUT (Texture::bind GL slots 4-5)
//   8: storage image (Texture::bind_as_image, compute writes)

Texture brdf_lut_texture;

struct {
    std::shared_ptr<Texture> black;
    std::shared_ptr<Texture> white;
    std::shared_ptr<Texture> normal;
    std::shared_ptr<Texture> metal_rough;
} default_textures;

bool audit_bindings_before_draw = false;

// void debug_out(GLenum, GLenum type, GLuint, GLenum sev, GLsizei, const char* msg, const void*) {
//     if(sev == GL_DEBUG_SEVERITY_NOTIFICATION) {
//         return;
//     }

//     const bool is_error = (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR);
//     (is_error ? std::cerr : std::cout) << (sev == GL_DEBUG_SEVERITY_HIGH ? "[GL][HIGH] " : "[GL] ") << msg << std::endl;

//     if(is_error) {
//         break_in_debugger();
//     }
// }

// u32 buffer_usage_to_gl(BufferUsage usage) {
//     switch(usage) {
//         case BufferUsage::Attribute:
//             return GL_ARRAY_BUFFER;

//         case BufferUsage::Index:
//             return GL_ELEMENT_ARRAY_BUFFER;

//         case BufferUsage::Uniform:
//             return GL_UNIFORM_BUFFER;

//         case BufferUsage::Storage:
//             return GL_SHADER_STORAGE_BUFFER;
//     }

//     FATAL("Unknown usage value");
// }

// u32 access_type_to_gl(AccessType access) {
//     switch(access) {
//         case AccessType::WriteOnly:
//             return GL_WRITE_ONLY;

//         case AccessType::ReadOnly:
//             return GL_READ_ONLY;

//         case AccessType::ReadWrite:
//             return GL_READ_WRITE;
//     }

//     FATAL("Unknown access type value");
// }


// u32 align_up_to(u32 val, u32 up_to) {
//     if(const u32 diff = val % up_to) {
//         return val + up_to - diff;
//     }
//     return val;
// }



void init_graphics(GLFWwindow* window) {
    vk_init(window);

//     glClearColor(0.5f, 0.7f, 0.8f, 0.0f);

//     {
//         glDebugMessageCallback(&debug_out, nullptr);

//         glEnable(GL_DEBUG_OUTPUT);
// #ifdef OS_WIN
//         if(running_in_debugger()) {
//             glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
//         }
// #endif
//     }

//     {
//         // Set depth range to [0; 1] instead of the default [-1; 1]
//         glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
//         // We use reverse-Z so far is 0
//         glClearDepthf(0.0f);
//     }

//     glActiveTexture(GL_TEXTURE0);
//     glEnable(GL_FRAMEBUFFER_SRGB);
//     glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

//     GLuint global_vao = 0;
//     glGenVertexArrays(1, &global_vao);
//     glBindVertexArray(global_vao);

    {
        // Split-sum IBL: a 256² RG LUT of the BRDF scale/bias terms. Written once
        // with a compute shader before the first frame (OpenGL did this with
        // glDispatchCompute; we use immediate_submit so there is a command buffer).
        brdf_lut_texture = Texture(glm::uvec2(256), ImageFormat::RG16_UNORM, WrapMode::Clamp);

        std::shared_ptr<Program> brdf_program = Program::from_file("brdf.comp");
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

    ctx().vertex_input = VertexLayout::None;
    if(ctx().bound_program) {
        ctx().bound_program->bind();
        ctx().bound_program->flush_push_constants();
    }

    flush_descriptor_bindings();

    if(!ctx().frame_active) {
        return;
    }

    // No vertex buffer: screen.slang uses SV_VertexID (formerly gl_VertexID).
    vkCmdDraw(vk_command_buffer(), 3, 1, 0, 0);
}

void blit_to_screen(const Texture& tex) {
    const std::shared_ptr<Program> blit_program = Program::from_files("passthrough.frag", "screen.vert");

    // Default Framebuffer = current swapchain view (GL FBO 0). Left open so ImGui can draw
    // into the same rendering with loadOp LOAD on a later bind; end_frame closes it.
    Framebuffer().bind(false, false);

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
    // OpenGL queried GL_CURRENT_PROGRAM and walked uniform/block interfaces.
    // Vulkan has no global "current program"; we mirror GL with sticky state on GraphicsContext.
    ALWAYS_ASSERT(ctx().bound_program, "No pipeline bound (call Program::bind before drawing)");
    ALWAYS_ASSERT(ctx().rendering_active, "No active rendering pass (call Framebuffer::bind first)");

    if(ctx().vertex_input == VertexLayout::Mesh) {
        ALWAYS_ASSERT(ctx().bound_vertex.buffer, "No vertex buffer bound");
        ALWAYS_ASSERT(ctx().bound_index.buffer, "No index buffer bound");
    }

    for(u32 binding = 0; binding < descriptor_binding_count; ++binding) {
        const BoundBuffer& bound = ctx().bound_descriptors[binding];
        if(bound.buffer) {
            ALWAYS_ASSERT(bound.size > 0, "Bound descriptor buffer has zero size");
        }
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
