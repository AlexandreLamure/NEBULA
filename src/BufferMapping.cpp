#include "BufferMapping.h"

namespace nebula {

BufferMappingBase::BufferMappingBase(BufferMappingBase&& other) {
    swap(other);
}

BufferMappingBase& BufferMappingBase::operator=(BufferMappingBase&& other) {
    swap(other);
    return *this;
}

BufferMappingBase::~BufferMappingBase() {
    // Persistent map: do not unmap. Non-coherent memory must be flushed so the
    // GPU sees the CPU writes at the next submit.
    if(_needs_flush && _allocation && device_allocator()) {
        vk_check(vmaFlushAllocation(device_allocator(), _allocation, 0, VK_WHOLE_SIZE));
    }
}

void BufferMappingBase::swap(BufferMappingBase& other) {
    std::swap(_allocation, other._allocation);
    std::swap(_byte_size, other._byte_size);
    std::swap(_data, other._data);
    std::swap(_needs_flush, other._needs_flush);
}

}
