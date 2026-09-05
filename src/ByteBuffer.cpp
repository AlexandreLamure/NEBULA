#include "ByteBuffer.h"

#include <cstring>

namespace nebula {

struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info = {};
};

static AllocatedBuffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags, VmaMemoryUsage memory_usage) {
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo alloc_ci{
        .flags = flags,
        .usage = memory_usage,
    };

    AllocatedBuffer out;
    vk_check(vmaCreateBuffer(device_allocator(), &buffer_ci, &alloc_ci, &out.buffer, &out.allocation, &out.info));
    return out;
}

// HOST_VISIBLE memory still needs an explicit flush unless it is also HOST_COHERENT.
static bool allocation_needs_flush(VmaAllocation allocation) {
    VkMemoryPropertyFlags mem_flags = 0;
    vmaGetAllocationMemoryProperties(device_allocator(), allocation, &mem_flags);
    return !(mem_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

static void upload_via_staging(VkBuffer dst, const void* data, size_t size) {
    AllocatedBuffer staging = create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        VMA_MEMORY_USAGE_AUTO
    );

    ALWAYS_ASSERT(staging.info.pMappedData, "Staging buffer is not mapped");
    std::memcpy(staging.info.pMappedData, data, size);
    if(allocation_needs_flush(staging.allocation)) {
        vk_check(vmaFlushAllocation(device_allocator(), staging.allocation, 0, VK_WHOLE_SIZE));
    }

    immediate_submit([&](VkCommandBuffer cmd) {
        const VkBufferCopy copy{.size = size};
        vkCmdCopyBuffer(cmd, staging.buffer, dst, 1, &copy);
    });

    vmaDestroyBuffer(device_allocator(), staging.buffer, staging.allocation);
}

ByteBuffer::ByteBuffer(ByteBuffer&& other) {
    swap(other);
}

ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) {
    swap(other);
    return *this;
}

void ByteBuffer::swap(ByteBuffer& other) {
    std::swap(_buffer, other._buffer);
    std::swap(_allocation, other._allocation);
    std::swap(_mapped, other._mapped);
    std::swap(_size, other._size);
    std::swap(_needs_flush, other._needs_flush);
}

ByteBuffer::ByteBuffer(const void* data, size_t size) : _size(size) {
    ALWAYS_ASSERT(_size, "Buffer size can not be 0");

    // TODO: this heuristic is not great. I should build a proper system to declare which are used for which purposes.
    if(data) {
        // Mesh vertex/index: device-local, uploaded once through a staging buffer.
        AllocatedBuffer gpu = create_buffer(
            size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );
        _buffer = gpu.buffer;
        _allocation = gpu.allocation;
        upload_via_staging(_buffer, data, size);
    } else {
        // Uniform/storage (and ImGui vertex/index): persistently mapped, written every frame.
        // Usage is not declared at create time, so allow every bind target this class supports.
        AllocatedBuffer host = create_buffer(
            size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );
        _buffer = host.buffer;
        _allocation = host.allocation;
        _mapped = host.info.pMappedData;
        ALWAYS_ASSERT(_mapped, "Host-visible buffer is not mapped");
        _needs_flush = allocation_needs_flush(_allocation);
    }
}

ByteBuffer::~ByteBuffer() {
    defer_destroy(_buffer, _allocation);
    _buffer = VK_NULL_HANDLE;
    _allocation = nullptr;
    _mapped = nullptr;
}

size_t ByteBuffer::byte_size() const {
    return _size;
}

BufferMapping<byte> ByteBuffer::map_bytes(AccessType access) {
    return BufferMapping<byte>(map_internal(access), byte_size(), allocation(), mapping_needs_flush());
}

void* ByteBuffer::map_internal(AccessType) {
    DEBUG_ASSERT(_buffer && _size);
    ALWAYS_ASSERT(_mapped, "This buffer is device-local and cannot be mapped");
    return _mapped;
}

VmaAllocation ByteBuffer::allocation() const {
    return _allocation;
}

bool ByteBuffer::mapping_needs_flush() const {
    return _needs_flush;
}

}
