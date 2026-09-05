#ifndef TYPEDBUFFER_H
#define TYPEDBUFFER_H

#include <ByteBuffer.h>

#include <vector>

namespace nebula {

template<typename T>
class TypedBuffer : public ByteBuffer {
    public:
        TypedBuffer() = default;

        TypedBuffer(Span<const T> data) : TypedBuffer(data.data(), data.size()) {
        }

        TypedBuffer(const T* data, size_t count) : ByteBuffer(data, count * sizeof(T)) {
        }

        size_t elementCount() const {
            DEBUG_ASSERT(byteSize() % sizeof(T) == 0);
            return byteSize() / sizeof(T);
        }

        BufferMapping<T> map(AccessType access = AccessType::ReadWrite) {
            return BufferMapping<T>(ByteBuffer::mapInternal(access), byteSize(), allocation(), mappingNeedsFlush());
        }
};

}

#endif // TYPEDBUFFER_H
