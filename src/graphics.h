#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <utils.h>

#include <string_view>
#include <memory>

struct GLFWwindow;

namespace OM3D {

class Texture;

enum class BufferUsage {
    Attribute,
    Index,
    Uniform,
    Storage,
};

enum class AccessType {
    WriteOnly,
    ReadOnly,
    ReadWrite
};

void init_graphics(GLFWwindow* window);
void destroy_graphics();

void begin_frame();
void end_frame();

void audit_bindings();

const Texture& brdf_lut();

void draw_full_screen_triangle();
void blit_to_screen(const Texture& tex);

std::shared_ptr<Texture> default_black_texture();
std::shared_ptr<Texture> default_white_texture();
std::shared_ptr<Texture> default_normal_texture();
std::shared_ptr<Texture> default_metal_rough_texture();

}

#endif // GRAPHICS_H
