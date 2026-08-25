#ifndef BUFFERMAPPING_H
#define BUFFERMAPPING_H

#include <graphics.h>
#include <VkContext.h>

namespace OM3D {

class BufferMappingBase : NonCopyable {
    public:
        BufferMappingBase(BufferMappingBase&& other);
        BufferMappingBase& operator=(BufferMappingBase&& other);

        ~BufferMappingBase();

    protected:
        BufferMappingBase() = default;

        void swap(BufferMappingBase& other);

        VmaAllocation _allocation = nullptr;
        size_t _byte_size = 0;
        void* _data = nullptr;
        bool _needs_flush = false;
};

template<typename T>
class BufferMapping : BufferMappingBase {
    public:
        T* data() {
            return static_cast<T*>(_data);
        }

        size_t byte_size() const {
            return _byte_size;
        }

        size_t element_count() const {
            return _byte_size / sizeof(T);
        }

        T& operator[](size_t index) {
            DEBUG_ASSERT(index < element_count());
            return data()[index];
        }

    private:
        friend class ByteBuffer;

        template<typename U>
        friend class TypedBuffer;

        BufferMapping(void* data, size_t size, VmaAllocation allocation, bool needs_flush) {
            _data = data;
            _byte_size = size;
            _allocation = allocation;
            _needs_flush = needs_flush;
            ALWAYS_ASSERT(size % sizeof(T) == 0, "Element size doesn't divide buffer size");
        }
};

}

#endif // BUFFERMAPPING_H
