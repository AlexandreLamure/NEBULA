#ifndef IMAGEFORMAT_H
#define IMAGEFORMAT_H

#include <utils.h>

#include <volk.h>

namespace nebula {

enum class ImageFormat {

    RGBA8_UNORM,
    RGBA8_sRGB,
    RGB8_UNORM,
    RGB8_sRGB,

    RG16_UNORM,

    RGBA16_FLOAT,
    Depth32_FLOAT
};

VkFormat imageFormatToVk(ImageFormat format);

}

#endif // IMAGEFORMAT_H
