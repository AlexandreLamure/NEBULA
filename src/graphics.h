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

void initGraphics(GLFWwindow* window);
void destroyGraphics();

void beginFrame();
void endFrame();

const Texture& brdfLut();

void drawMesh(
    const Program& program,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push,
    VkBuffer vbo,
    VkBuffer ibo,
    u32 indexCount
);

void drawFullscreen(
    const Program& program,
    const RasterState& raster,
    const PassResources& pass,
    const PushConstants& push
);

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
);

void dispatch(const Program& program, const PassResources& pass, u32 x, u32 y, u32 z);

void blitToScreen(const Texture& tex);

std::shared_ptr<Texture> defaultBlackTexture();
std::shared_ptr<Texture> defaultWhiteTexture();
std::shared_ptr<Texture> defaultNormalTexture();
std::shared_ptr<Texture> defaultMetalRoughTexture();

}

#endif // GRAPHICS_H
