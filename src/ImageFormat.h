#ifndef IMAGEFORMAT_H
#define IMAGEFORMAT_H

#include <utils.h>

#include <volk.h>

namespace OM3D {

enum class ImageFormat {
    RGBA8_UNORM,
    RGBA8_sRGB,
    RGB8_UNORM,
    RGB8_sRGB,

    RG16_UNORM,

    RGBA16_FLOAT,
    Depth32_FLOAT
};

VkFormat image_format_to_vk(ImageFormat format);

}

#endif // IMAGEFORMAT_H
