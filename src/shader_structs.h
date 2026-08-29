#ifndef SHADERSTRUCTS_H
#define SHADERSTRUCTS_H

#include <utils.h>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <cstddef>

namespace OM3D {
namespace shader {

// CPU copies of the GPU structs in shaders/structs.slang.
//
// OpenGL let us #include the GLSL into C++ (`using namespace glm` made vec3/mat4
// mean glm types). Slang is a different language, so that trick does not parse.
// Keep the two copies in sync: same field order, same padding.
//
// Vulkan uniform buffers use std140-like packing. A float3 is 12 bytes but
// aligns to 16, so we put an explicit float next to it instead of relying on
// compiler padding. glm::vec3 is 12 bytes, so `vec3 + float` is 16 — a match.

struct CameraData {
    glm::mat4 view_proj;
    glm::mat4 inv_view_proj;
    glm::vec3 position;
    float padding;
};

struct FrameData {
    CameraData camera;

    glm::vec3 sun_dir;
    u32 point_light_count;

    glm::vec3 sun_color;
    float ibl_intensity;
};

struct PointLight {
    glm::vec3 position;
    float radius;
    glm::vec3 color;
    float padding;
};

}
}

#endif // SHADERSTRUCTS_H
