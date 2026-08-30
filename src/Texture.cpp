#include "Texture.h"
#include "Program.h"
#include "VkContext.h"

#include <volk.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <cmath>
#include <algorithm>
#include <cstring>

namespace OM3D {

Result<TextureData> TextureData::from_file(const std::string& file) {
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

static VkImageAspectFlags aspect_for_format(ImageFormat format) {
    return format == ImageFormat::Depth32_FLOAT
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;
}

static size_t pixel_stride(ImageFormat format) {
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

static VkImageView create_image_view(
    VkImage image,
    VkFormat format,
    VkImageViewType view_type,
    u32 mip_levels,
    u32 base_layer,
    u32 layer_count,
    VkImageAspectFlags aspect
) {
    const VkImageViewCreateInfo view_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = view_type,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspect,
            .levelCount = mip_levels,
            .baseArrayLayer = base_layer,
            .layerCount = layer_count,
        },
    };

    VkImageView view = VK_NULL_HANDLE;
    vk_check(vkCreateImageView(vk_device(), &view_ci, nullptr, &view));
    return view;
}

static void texture_barrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage,
    VkAccessFlags2 src_access,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 dst_access,
    VkImageAspectFlags aspect,
    u32 base_mip,
    u32 mip_count,
    u32 base_layer,
    u32 layer_count
) {
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = base_mip,
            .levelCount = mip_count,
            .baseArrayLayer = base_layer,
            .layerCount = layer_count,
        },
    };
    const VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

void Texture::create_gpu_image(
    glm::uvec2 size,
    ImageFormat format,
    TextureType type,
    u32 mip_levels,
    VkImageUsageFlags usage
) {
    _size = size;
    _format = format;
    _type = type;
    _mip_levels = mip_levels;
    _layout = VK_IMAGE_LAYOUT_UNDEFINED;

    const VkFormat vk_format = image_format_to_vk(format);
    const u32 array_layers = type == TextureType::Cube ? 6u : 1u;
    const VkImageCreateFlags flags = type == TextureType::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    const VkImageAspectFlags aspect = aspect_for_format(format);

    const VkImageCreateInfo image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = flags,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vk_format,
        .extent = {size.x, size.y, 1},
        .mipLevels = mip_levels,
        .arrayLayers = array_layers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo alloc_ci{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    vk_check(vmaCreateImage(device_allocator(), &image_ci, &alloc_ci, &_image, &_allocation, nullptr));

    if(type == TextureType::Cube) {
        _view = create_image_view(_image, vk_format, VK_IMAGE_VIEW_TYPE_CUBE, mip_levels, 0, 6, aspect);
        // Compute writes a 2D array (six faces); sampling uses a cube view.
        _storage_view = create_image_view(_image, vk_format, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 1, 0, 6, aspect);
    } else {
        _view = create_image_view(_image, vk_format, VK_IMAGE_VIEW_TYPE_2D, mip_levels, 0, 1, aspect);
        _storage_view = VK_NULL_HANDLE;
    }
}

void Texture::upload_pixels(VkCommandBuffer cmd, VkBuffer staging_buffer, size_t byte_size) {
    const VkImageAspectFlags aspect = aspect_for_format(_format);
    const u32 layers = _type == TextureType::Cube ? 6u : 1u;

    texture_barrier(
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
    vkCmdCopyBufferToImage(cmd, staging_buffer, _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

void Texture::generate_mipmaps(VkCommandBuffer cmd) {
    if(_format == ImageFormat::Depth32_FLOAT) {
        return;
    }

    const VkImageAspectFlags aspect = aspect_for_format(_format);
    const u32 layers = _type == TextureType::Cube ? 6u : 1u;

    if(_mip_levels <= 1) {
        texture_barrier(
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

    i32 mip_width = i32(_size.x);
    i32 mip_height = i32(_size.y);

    for(u32 mip = 1; mip != _mip_levels; ++mip) {
        texture_barrier(
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

        const i32 next_width = std::max(1, mip_width / 2);
        const i32 next_height = std::max(1, mip_height / 2);

        const VkImageBlit blit{
            .srcSubresource = {
                .aspectMask = aspect,
                .mipLevel = mip - 1,
                .baseArrayLayer = 0,
                .layerCount = layers,
            },
            .srcOffsets = {
                {0, 0, 0},
                {mip_width, mip_height, 1},
            },
            .dstSubresource = {
                .aspectMask = aspect,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = layers,
            },
            .dstOffsets = {
                {0, 0, 0},
                {next_width, next_height, 1},
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

        mip_width = next_width;
        mip_height = next_height;
    }

    texture_barrier(
        cmd,
        _image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT,
        aspect,
        _mip_levels - 1,
        1,
        0,
        layers
    );

    texture_barrier(
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
        _mip_levels - 1,
        0,
        layers
    );

    _layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void Texture::finish_sampled_texture(const void* pixels, size_t byte_size) {
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_allocation = nullptr;
    {
        const VkBufferCreateInfo buffer_ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = byte_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo staging_alloc{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo info{};
        vk_check(vmaCreateBuffer(device_allocator(), &buffer_ci, &staging_alloc, &staging_buffer, &staging_allocation, &info));
        std::memcpy(info.pMappedData, pixels, byte_size);
    }

    immediate_submit([&](VkCommandBuffer cmd) {
        upload_pixels(cmd, staging_buffer, byte_size);
        generate_mipmaps(cmd);
    });

    vmaDestroyBuffer(device_allocator(), staging_buffer, staging_allocation);

    if(_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        _layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}

Texture::Texture(const TextureData& data) :
    _wrap(WrapMode::Repeat) {

    _size = data.size;
    _format = data.format;

    const u32 mips = mip_levels(_size);
    create_gpu_image(_size, _format, TextureType::Tex2D, mips, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    const size_t byte_size = _size.x * _size.y * pixel_stride(_format);
    finish_sampled_texture(data.data.get(), byte_size);
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

    create_gpu_image(size, format, TextureType::Tex2D, 1, usage);
}

Texture Texture::empty_cubemap(u32 size, ImageFormat format, u32 mipmaps) {
    Texture cube;
    cube._wrap = WrapMode::Clamp;

    const glm::uvec2 face_size(size);
    const u32 mips = std::min(mipmaps, mip_levels(face_size));
    cube.create_gpu_image(
        face_size,
        format,
        TextureType::Cube,
        mips,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
    );

    return cube;
}

Texture Texture::cubemap_from_equirec(const Texture& equirec) {
    const size_t px = equirec.size().x * equirec.size().y;
    const size_t per_face = px / 6;

    u32 face_size = 8;
    while(face_size * face_size < per_face) {
        face_size *= 2;
    }

    Texture cube = empty_cubemap(face_size, ImageFormat::RGBA16_FLOAT, 9999);

    immediate_submit([&](VkCommandBuffer cmd) {
        const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        // Compute writes mip 0 of all six faces as a 2D array (see _storage_view).
        texture_barrier(
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

        equirec.bind(0);
        cube.bind_as_image(1, AccessType::WriteOnly);
        const std::shared_ptr<Program> equirec_program = Program::from_file("equirec_cube.comp");
        equirec_program->bind();
        dispatch_compute(face_size / 8, face_size / 8, 6);

        texture_barrier(
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

        if(cube._mip_levels > 1) {
            texture_barrier(
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
                cube._mip_levels - 1,
                0,
                6
            );
        }

        cube._layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cube.generate_mipmaps(cmd);
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
    std::swap(_storage_view, other._storage_view);
    std::swap(_layout, other._layout);
    std::swap(_size, other._size);
    std::swap(_mip_levels, other._mip_levels);
    std::swap(_format, other._format);
    std::swap(_wrap, other._wrap);
    std::swap(_type, other._type);
}

void Texture::destroy() {
    defer_destroy(_view);
    if(_storage_view && _storage_view != _view) {
        defer_destroy(_storage_view);
    }
    defer_destroy(_image, _allocation);

    _image = VK_NULL_HANDLE;
    _allocation = nullptr;
    _view = VK_NULL_HANDLE;
    _storage_view = VK_NULL_HANDLE;
    _layout = VK_IMAGE_LAYOUT_UNDEFINED;
    _size = {};
    _mip_levels = 1;
}

bool Texture::is_null() const {
    return !_image;
}

void Texture::bind(u32 index) const {
    if(index < gl_texture_slot_count) {
        ctx().bound_textures[index].texture = this;
    }
}

void Texture::bind_as_image(u32 index, AccessType) {
    (void)index;
    _layout = VK_IMAGE_LAYOUT_GENERAL;
    ctx().bound_storage_image = {this, VK_IMAGE_LAYOUT_GENERAL};
}

TextureType Texture::texture_type() const {
    return _type;
}

glm::uvec2 Texture::size() const {
    return _size;
}

u32 Texture::mip_levels(glm::uvec2 size) {
    const float side = float(std::max(size.x, size.y));
    return 1 + u32(std::floor(std::log2(side)));
}

bool Texture::is_cube() const {
    return _type == TextureType::Cube;
}

}
