#include "Texture.h"
#include "Program.h"
#include "VkContext.h"

#include <volk.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <cmath>
#include <algorithm>
#include <cstring>

namespace nebula {

Result<TextureData> TextureData::fromFile(const std::string& file) {
    int width = 0;
    int height = 0;
    int channels = 0;
    u8* img = stbi_load(file.c_str(), &width, &height, &channels, 4);
    DEFER(stbi_image_free(img));
    if(!img || width <= 0 || height <= 0 || channels <= 0) {
        return {false, {}};
    }

    const size_t bytes = width * height * 4;

    TextureData data;
    data.size = glm::uvec2(width, height);
    data.format = ImageFormat::RGBA8_UNORM;
    data.data = std::make_unique<u8[]>(bytes);
    std::copy_n(img, bytes, data.data.get());

    return {true, std::move(data)};
}

static VkImageAspectFlags aspectForFormat(ImageFormat format) {
    return format == ImageFormat::Depth32_FLOAT
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;
}

static size_t pixelStride(ImageFormat format) {
    switch(format) {
        case ImageFormat::RGBA8_UNORM:
        case ImageFormat::RGBA8_sRGB:
        case ImageFormat::RGB8_UNORM:
        case ImageFormat::RGB8_sRGB:
        case ImageFormat::Depth32_FLOAT:
            return 4;
        case ImageFormat::RG16_UNORM:
            return 4;
        case ImageFormat::RGBA16_FLOAT:
            return 8;
    }

    FATAL("Unknown image format");
}

static VkImageView createImageView(
    VkImage image,
    VkFormat format,
    VkImageViewType viewType,
    u32 mipLevels,
    u32 baseLayer,
    u32 layerCount,
    VkImageAspectFlags aspect
) {
    const VkImageViewCreateInfo viewCi{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = viewType,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspect,
            .levelCount = mipLevels,
            .baseArrayLayer = baseLayer,
            .layerCount = layerCount,
        },
    };

    VkImageView view = VK_NULL_HANDLE;
    vkCheck(vkCreateImageView(vkDevice(), &viewCi, nullptr, &view));
    return view;
}

static void textureBarrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess,
    VkImageAspectFlags aspect,
    u32 baseMip,
    u32 mipCount,
    u32 baseLayer,
    u32 layerCount
) {
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = baseMip,
            .levelCount = mipCount,
            .baseArrayLayer = baseLayer,
            .layerCount = layerCount,
        },
    };
    const VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

void Texture::createGpuImage(
    glm::uvec2 size,
    ImageFormat format,
    TextureType type,
    u32 mipLevels,
    VkImageUsageFlags usage
) {
    _size = size;
    _format = format;
    _type = type;
    _mipLevels = mipLevels;
    _layout = VK_IMAGE_LAYOUT_UNDEFINED;

    const VkFormat vkFormat = imageFormatToVk(format);
    const u32 arrayLayers = type == TextureType::Cube ? 6u : 1u;
    const VkImageCreateFlags flags = type == TextureType::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    const VkImageAspectFlags aspect = aspectForFormat(format);

    const VkImageCreateInfo imageCi{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = flags,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vkFormat,
        .extent = {size.x, size.y, 1},
        .mipLevels = mipLevels,
        .arrayLayers = arrayLayers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocCi{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    vkCheck(vmaCreateImage(deviceAllocator(), &imageCi, &allocCi, &_image, &_allocation, nullptr));

    if(type == TextureType::Cube) {
        _view = createImageView(_image, vkFormat, VK_IMAGE_VIEW_TYPE_CUBE, mipLevels, 0, 6, aspect);
        // Compute writes a 2D array (six faces); sampling uses a cube view.
        _storageView = createImageView(_image, vkFormat, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 1, 0, 6, aspect);
    } else {
        _view = createImageView(_image, vkFormat, VK_IMAGE_VIEW_TYPE_2D, mipLevels, 0, 1, aspect);
        _storageView = VK_NULL_HANDLE;
    }
}

void Texture::uploadPixels(VkCommandBuffer cmd, VkBuffer stagingBuffer, size_t byteSize) {
    const VkImageAspectFlags aspect = aspectForFormat(_format);
    const u32 layers = _type == TextureType::Cube ? 6u : 1u;

    textureBarrier(
        cmd,
        _image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        0,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        aspect,
        0,
        1,
        0,
        layers
    );

    const VkBufferImageCopy copy{
        .bufferOffset = 0,
        .imageSubresource = {
            .aspectMask = aspect,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = layers,
        },
        .imageExtent = {_size.x, _size.y, 1},
    };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

// Blits each mip from the previous one, toggling TRANSFER_SRC/DST layouts as it goes.
void Texture::generateMipmaps(VkCommandBuffer cmd) {
    if(_format == ImageFormat::Depth32_FLOAT) {
        return;
    }

    const VkImageAspectFlags aspect = aspectForFormat(_format);
    const u32 layers = _type == TextureType::Cube ? 6u : 1u;

    if(_mipLevels <= 1) {
        textureBarrier(
            cmd,
            _image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            aspect,
            0,
            1,
            0,
            layers
        );
        _layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return;
    }

    i32 mipWidth = i32(_size.x);
    i32 mipHeight = i32(_size.y);

    for(u32 mip = 1; mip != _mipLevels; ++mip) {
        textureBarrier(
            cmd,
            _image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
            aspect,
            mip - 1,
            1,
            0,
            layers
        );

        textureBarrier(
            cmd,
            _image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            aspect,
            mip,
            1,
            0,
            layers
        );

        const i32 nextWidth = std::max(1, mipWidth / 2);
        const i32 nextHeight = std::max(1, mipHeight / 2);

        const VkImageBlit blit{
            .srcSubresource = {
                .aspectMask = aspect,
                .mipLevel = mip - 1,
                .baseArrayLayer = 0,
                .layerCount = layers,
            },
            .srcOffsets = {
                {0, 0, 0},
                {mipWidth, mipHeight, 1},
            },
            .dstSubresource = {
                .aspectMask = aspect,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = layers,
            },
            .dstOffsets = {
                {0, 0, 0},
                {nextWidth, nextHeight, 1},
            },
        };
        vkCmdBlitImage(
            cmd,
            _image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            _image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR
        );

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    textureBarrier(
        cmd,
        _image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        aspect,
        _mipLevels - 1,
        1,
        0,
        layers
    );

    textureBarrier(
        cmd,
        _image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        aspect,
        0,
        _mipLevels - 1,
        0,
        layers
    );

    _layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void Texture::finishSampledTexture(const void* pixels, size_t byteSize) {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    {
        const VkBufferCreateInfo bufferCi{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = byteSize,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo stagingAlloc{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo info{};
        vkCheck(vmaCreateBuffer(deviceAllocator(), &bufferCi, &stagingAlloc, &stagingBuffer, &stagingAllocation, &info));
        std::memcpy(info.pMappedData, pixels, byteSize);
    }

    immediateSubmit([&](VkCommandBuffer cmd) {
        uploadPixels(cmd, stagingBuffer, byteSize);
        generateMipmaps(cmd);
    });

    vmaDestroyBuffer(deviceAllocator(), stagingBuffer, stagingAllocation);

    if(_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        _layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}

Texture::Texture(const TextureData& data) :
    _wrap(WrapMode::Repeat) {

    _size = data.size;
    _format = data.format;

    const u32 mips = mipLevels(_size);
    createGpuImage(_size, _format, TextureType::Tex2D, mips, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    const size_t byteSize = _size.x * _size.y * pixelStride(_format);
    finishSampledTexture(data.data.get(), byteSize);
}

Texture::Texture(const glm::uvec2 &size, ImageFormat format, WrapMode wrap) :
    _wrap(wrap) {

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if(format == ImageFormat::Depth32_FLOAT) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        // Compute (BRDF LUT) writes empty color images as storage.
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    createGpuImage(size, format, TextureType::Tex2D, 1, usage);
}

Texture Texture::emptyCubemap(u32 size, ImageFormat format, u32 mipmaps) {
    Texture cube;
    cube._wrap = WrapMode::Clamp;

    const glm::uvec2 faceSize(size);
    const u32 mips = std::min(mipmaps, mipLevels(faceSize));
    cube.createGpuImage(
        faceSize,
        format,
        TextureType::Cube,
        mips,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
    );

    return cube;
}

// Renders each cubemap face from the lat-long map via compute, then generates mips for IBL.
Texture Texture::cubemapFromEquirec(const Texture& equirec) {
    const size_t px = equirec.size().x * equirec.size().y;
    const size_t perFace = px / 6;

    u32 faceSize = 8;
    while(faceSize * faceSize < perFace) {
        faceSize *= 2;
    }

    Texture cube = emptyCubemap(faceSize, ImageFormat::RGBA16_FLOAT, 9999);

    immediateSubmit([&](VkCommandBuffer cmd) {
        const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        // Compute writes mip 0 of all six faces as a 2D array (see _storageView).
        textureBarrier(
            cmd,
            cube._image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            aspect,
            0,
            1,
            0,
            6
        );
        cube._layout = VK_IMAGE_LAYOUT_GENERAL;

        PassResources pass{};
        pass.textures[0] = &equirec;
        pass.storageImage = &cube;
        const std::shared_ptr<Program> equirecProgram = Program::fromFile("equirecCube.slang");
        dispatch(*equirecProgram, pass, faceSize / 8, faceSize / 8, 6);

        textureBarrier(
            cmd,
            cube._image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            aspect,
            0,
            1,
            0,
            6
        );

        if(cube._mipLevels > 1) {
            textureBarrier(
                cmd,
                cube._image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                0,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                aspect,
                1,
                cube._mipLevels - 1,
                0,
                6
            );
        }

        cube._layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cube.generateMipmaps(cmd);
    });

    return cube;
}

Texture::Texture(Texture&& other) noexcept {
    swap(other);
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if(this != &other) {
        destroy();
        swap(other);
    }
    return *this;
}

Texture::~Texture() {
    destroy();
}

void Texture::swap(Texture& other) {
    std::swap(_image, other._image);
    std::swap(_allocation, other._allocation);
    std::swap(_view, other._view);
    std::swap(_storageView, other._storageView);
    std::swap(_layout, other._layout);
    std::swap(_size, other._size);
    std::swap(_mipLevels, other._mipLevels);
    std::swap(_format, other._format);
    std::swap(_wrap, other._wrap);
    std::swap(_type, other._type);
}

void Texture::destroy() {
    deferDestroy(_view);
    if(_storageView && _storageView != _view) {
        deferDestroy(_storageView);
    }
    deferDestroy(_image, _allocation);

    _image = VK_NULL_HANDLE;
    _allocation = nullptr;
    _view = VK_NULL_HANDLE;
    _storageView = VK_NULL_HANDLE;
    _layout = VK_IMAGE_LAYOUT_UNDEFINED;
    _size = {};
    _mipLevels = 1;
}

bool Texture::isNull() const {
    return !_image;
}

TextureType Texture::textureType() const {
    return _type;
}

glm::uvec2 Texture::size() const {
    return _size;
}

u32 Texture::mipLevels(glm::uvec2 size) {
    const float side = float(std::max(size.x, size.y));
    return 1 + u32(std::floor(std::log2(side)));
}

bool Texture::isCube() const {
    return _type == TextureType::Cube;
}

}
