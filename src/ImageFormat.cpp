#include "ImageFormat.h"

#include <volk.h>

namespace nebula {

// RGB8 maps to RGBA8 — RGB8 is poorly supported as an optimal Vulkan format.
VkFormat image_format_to_vk(ImageFormat format) {
    switch(format) {
        case ImageFormat::RGBA8_UNORM:   return VK_FORMAT_R8G8B8A8_UNORM;
        case ImageFormat::RGBA8_sRGB:    return VK_FORMAT_R8G8B8A8_SRGB;
        case ImageFormat::RGB8_UNORM:    return VK_FORMAT_R8G8B8A8_UNORM;
        case ImageFormat::RGB8_sRGB:     return VK_FORMAT_R8G8B8A8_SRGB;
        case ImageFormat::RG16_UNORM:    return VK_FORMAT_R16G16_UNORM;
        case ImageFormat::RGBA16_FLOAT:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case ImageFormat::Depth32_FLOAT: return VK_FORMAT_D32_SFLOAT;
    }

    FATAL("Unknown image format");
}

}
