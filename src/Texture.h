#ifndef TEXTURE_H
#define TEXTURE_H

#include <graphics.h>
#include <ImageFormat.h>
#include <VkContext.h>

#include <volk.h>

#include <glm/vec2.hpp>

#include <vector>
#include <memory>


namespace nebula {

struct TextureData {
    std::unique_ptr<u8[]> data;
    glm::uvec2 size = {};
    ImageFormat format;

    static Result<TextureData> from_file(const std::string& file_name);
};



enum class WrapMode {
    Repeat,
    Clamp,
};

enum class TextureType {
    Tex2D,
    Cube,
};

class Texture {

    public:
        Texture() = default;

        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&& other) noexcept;

        ~Texture();

        Texture(const TextureData& data);

        Texture(const glm::uvec2 &size, ImageFormat format, WrapMode wrap);

        static Texture empty_cubemap(u32 size, ImageFormat format, u32 mipmaps = 1);
        static Texture cubemap_from_equirec(const Texture& equirec);

        bool is_null() const;

        TextureType texture_type() const;

        glm::uvec2 size() const;

        static u32 mip_levels(glm::uvec2 size);

        VkImage vk_image() const { return _image; }
        VkImageView vk_view() const { return _view; }
        VkImageView vk_storage_view() const { return _storage_view ? _storage_view : _view; }
        VkImageLayout vk_layout() const { return _layout; }
        void set_vk_layout(VkImageLayout layout) { _layout = layout; }
        WrapMode wrap_mode() const { return _wrap; }
        VkFormat vk_format() const { return image_format_to_vk(_format); }
        bool is_cube() const;

    private:
        void swap(Texture& other);
        void destroy();

        void create_gpu_image(glm::uvec2 size, ImageFormat format, TextureType type, u32 mip_levels, VkImageUsageFlags usage);
        void upload_pixels(VkCommandBuffer cmd, VkBuffer staging_buffer, size_t byte_size);
        void generate_mipmaps(VkCommandBuffer cmd);
        void finish_sampled_texture(const void* pixels, size_t byte_size);

        // Image + view + tracked layout for sampling and dynamic rendering.
        VkImage _image = VK_NULL_HANDLE;
        VmaAllocation _allocation = nullptr;
        VkImageView _view = VK_NULL_HANDLE;
        VkImageView _storage_view = VK_NULL_HANDLE;
        VkImageLayout _layout = VK_IMAGE_LAYOUT_UNDEFINED;

        glm::uvec2 _size = {};
        u32 _mip_levels = 1;
        ImageFormat _format;
        WrapMode _wrap = WrapMode::Repeat;
        TextureType _type = TextureType::Tex2D;
};

}

#endif // TEXTURE_H
