#ifndef BYTEBUFFER_H
#define BYTEBUFFER_H

#include <graphics.h>
#include <BufferMapping.h>
#include <VkContext.h>

namespace OM3D {

class ByteBuffer : NonCopyable {

    public:
        ByteBuffer() = default;
        ByteBuffer(ByteBuffer&& other);
        ByteBuffer& operator=(ByteBuffer&& other);

        ByteBuffer(const void* data, size_t size);
        ~ByteBuffer();

        void bind(BufferUsage usage) const;
        void bind(BufferUsage usage, u32 index) const;

        size_t byte_size() const;

        BufferMapping<byte> map_bytes(AccessType access = AccessType::ReadWrite);

    protected:
        void* map_internal(AccessType access);
        VmaAllocation allocation() const;
        bool mapping_needs_flush() const;

    private:
        void swap(ByteBuffer& other);

        VkBuffer _buffer = VK_NULL_HANDLE;
        VmaAllocation _allocation = nullptr;
        void* _mapped = nullptr;
        size_t _size = 0;
        bool _needs_flush = false;
};

}

#endif // BYTEBUFFER_H
