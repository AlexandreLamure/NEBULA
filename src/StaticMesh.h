#ifndef STATICMESH_H
#define STATICMESH_H

#include <graphics.h>
#include <TypedBuffer.h>
#include <Vertex.h>
#include <geometry.h>

#include <vector>

namespace nebula {


struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
};

class StaticMesh : NonCopyable {

    public:
        StaticMesh() = default;
        StaticMesh(StaticMesh&&) = default;
        StaticMesh& operator=(StaticMesh&&) = default;

        StaticMesh(const MeshData& data);

        VkBuffer vertexBuffer() const { return _vertexBuffer.vkBuffer(); }
        VkBuffer indexBuffer() const { return _indexBuffer.vkBuffer(); }
        u32 indexCount() const { return u32(_indexBuffer.elementCount()); }

        const Sphere& boundingSphereMs() const { return _boundingSphereMs; }

    private:
        TypedBuffer<Vertex> _vertexBuffer;
        TypedBuffer<u32> _indexBuffer;
        Sphere _boundingSphereMs;
};

}

#endif // STATICMESH_H
