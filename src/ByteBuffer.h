#ifndef BYTEBUFFER_H
#define BYTEBUFFER_H

#include <graphics.h>
#include <BufferMapping.h>
#include <VkContext.h>

namespace nebula {

class ByteBuffer : NonCopyable {

    public:
        ByteBuffer() = default;
        ByteBuffer(ByteBuffer&& other);
        ByteBuffer& operator=(ByteBuffer&& other);

        ByteBuffer(const void* data, size_t size);
        ~ByteBuffer();

        VkBuffer vkBuffer() const { return _buffer; }

        size_t byteSize() const;

        BufferMapping<byte> mapBytes(AccessType access = AccessType::ReadWrite);

    protected:
        void* mapInternal(AccessType access);
        VmaAllocation allocation() const;
        bool mappingNeedsFlush() const;

    private:
        void swap(ByteBuffer& other);

        VkBuffer _buffer = VK_NULL_HANDLE;
        VmaAllocation _allocation = nullptr;
        void* _mapped = nullptr;
        size_t _size = 0;
        bool _needsFlush = false;
};

}

#endif // BYTEBUFFER_H
