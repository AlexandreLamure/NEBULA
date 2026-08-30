#include "StaticMesh.h"

#include "VkContext.h"
#include "Program.h"

#include <volk.h>

namespace OM3D {

extern bool audit_bindings_before_draw;

StaticMesh::StaticMesh(const MeshData& data) :
    _vertex_buffer(data.vertices),
    _index_buffer(data.indices) {
}

void StaticMesh::draw() const {
    ctx().vertex_input = VertexLayout::Mesh;
    if(ctx().bound_program) {
        ctx().bound_program->bind();
        ctx().bound_program->flush_push_constants();
    }

    _vertex_buffer.bind(BufferUsage::Attribute);
    _index_buffer.bind(BufferUsage::Index);

    if(audit_bindings_before_draw) {
        audit_bindings();
    }

    flush_descriptor_bindings();

    if(!ctx().frame_active) {
        return;
    }

    // Vertex layout is baked into the pipeline (see Program). Here we only bind which buffers to pull from.
    const VkCommandBuffer cmd = vk_command_buffer();
    const VkBuffer vertex = ctx().bound_vertex.buffer;
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex, &offset);
    vkCmdBindIndexBuffer(cmd, ctx().bound_index.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, u32(_index_buffer.element_count()), 1, 0, 0, 0);
}

}
