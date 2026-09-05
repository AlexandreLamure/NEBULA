#ifndef BUFFERMAPPING_H
#define BUFFERMAPPING_H

#include <graphics.h>
#include <VkContext.h>

namespace nebula {

class BufferMappingBase : NonCopyable {
    public:
        BufferMappingBase(BufferMappingBase&& other);
        BufferMappingBase& operator=(BufferMappingBase&& other);

        ~BufferMappingBase();

    protected:
        BufferMappingBase() = default;

        void swap(BufferMappingBase& other);

        VmaAllocation _allocation = nullptr;
        size_t _byteSize = 0;
        void* _data = nullptr;
        bool _needsFlush = false;
};

template<typename T>
class BufferMapping : BufferMappingBase {
    public:
        T* data() {
            return static_cast<T*>(_data);
        }

        size_t byteSize() const {
            return _byteSize;
        }

        size_t elementCount() const {
            return _byteSize / sizeof(T);
        }

        T& operator[](size_t index) {
            DEBUG_ASSERT(index < elementCount());
            return data()[index];
        }

    private:
        friend class ByteBuffer;

        template<typename U>
        friend class TypedBuffer;

        BufferMapping(void* data, size_t size, VmaAllocation allocation, bool needsFlush) {
            _data = data;
            _byteSize = size;
            _allocation = allocation;
            _needsFlush = needsFlush;
            ALWAYS_ASSERT(size % sizeof(T) == 0, "Element size doesn't divide buffer size");
        }
};

}

#endif // BUFFERMAPPING_H
