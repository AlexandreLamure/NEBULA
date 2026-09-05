#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <utils.h>
#include <VkContext.h>

#include <string_view>
#include <memory>

struct GLFWwindow;

namespace nebula {

class Texture;
class Program;
struct PushConstants;

enum class AccessType {
    WriteOnly,
    ReadOnly,
    ReadWrite
};

void init_graphics(GLFWwindow* window);
void destroy_graphics();

void begin_frame();
void end_frame();

const Texture& brdf_lut();

void draw_mesh(
    const Program& program,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push,
    VkBuffer vbo,
    VkBuffer ibo,
    u32 index_count
);

void draw_fullscreen(
    const Program& program,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push
);

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
);

void dispatch(const Program& program, const PassResources& pass, u32 x, u32 y, u32 z);

void blit_to_screen(const Texture& tex);

std::shared_ptr<Texture> default_black_texture();
std::shared_ptr<Texture> default_white_texture();
std::shared_ptr<Texture> default_normal_texture();
std::shared_ptr<Texture> default_metal_rough_texture();

}

#endif // GRAPHICS_H
