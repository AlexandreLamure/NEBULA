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

    static Result<TextureData> fromFile(const std::string& fileName);
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

        static Texture emptyCubemap(u32 size, ImageFormat format, u32 mipmaps = 1);
        static Texture cubemapFromEquirec(const Texture& equirec);

        bool isNull() const;

        TextureType textureType() const;

        glm::uvec2 size() const;

        static u32 mipLevels(glm::uvec2 size);

        VkImage vkImage() const { return _image; }
        VkImageView vkView() const { return _view; }
        VkImageView vkStorageView() const { return _storageView ? _storageView : _view; }
        VkImageLayout vkLayout() const { return _layout; }
        void setVkLayout(VkImageLayout layout) { _layout = layout; }
        WrapMode wrapMode() const { return _wrap; }
        VkFormat vkFormat() const { return imageFormatToVk(_format); }
        bool isCube() const;

    private:
        void swap(Texture& other);
        void destroy();

        void createGpuImage(glm::uvec2 size, ImageFormat format, TextureType type, u32 mipLevels, VkImageUsageFlags usage);
        void uploadPixels(VkCommandBuffer cmd, VkBuffer stagingBuffer, size_t byteSize);
        void generateMipmaps(VkCommandBuffer cmd);
        void finishSampledTexture(const void* pixels, size_t byteSize);

        // Image + view + tracked layout for sampling and dynamic rendering.
        VkImage _image = VK_NULL_HANDLE;
        VmaAllocation _allocation = nullptr;
        VkImageView _view = VK_NULL_HANDLE;
        VkImageView _storageView = VK_NULL_HANDLE;
        VkImageLayout _layout = VK_IMAGE_LAYOUT_UNDEFINED;

        glm::uvec2 _size = {};
        u32 _mipLevels = 1;
        ImageFormat _format;
        WrapMode _wrap = WrapMode::Repeat;
        TextureType _type = TextureType::Tex2D;
};

}

#endif // TEXTURE_H
