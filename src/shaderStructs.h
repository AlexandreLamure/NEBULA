#ifndef SHADERSTRUCTS_H
#define SHADERSTRUCTS_H

#include <utils.h>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <cstddef>

namespace nebula {
namespace shader {

// CPU copies of the GPU structs in shaders/structs.slang.
// Keep the two copies in sync: same field order, same padding.
//
// Vulkan uniform buffers use std140-like packing. A float3 is 12 bytes but
// aligns to 16, so we put an explicit float next to it instead of relying on
// compiler padding. glm::vec3 is 12 bytes, so `vec3 + float` is 16 — a match.

struct CameraData {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec3 position;
    float padding;
};

struct FrameData {
    CameraData camera;

    glm::vec3 sunDir;
    u32 pointLightCount;

    glm::vec3 sunColor;
    float iblIntensity;
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
