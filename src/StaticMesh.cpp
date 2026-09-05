#include "StaticMesh.h"

namespace nebula {

StaticMesh::StaticMesh(const MeshData& data) :
    _vertexBuffer(data.vertices),
    _indexBuffer(data.indices) {

    // Compute bounding sphere
    _boundingSphereMs._center = glm::vec3(0.0f);
    _boundingSphereMs._radius = 0.0f;
    for(const Vertex& vertex : data.vertices) {
        _boundingSphereMs._center += vertex.position;
    }
    _boundingSphereMs._center /= data.vertices.size();
    for(const Vertex& vertex : data.vertices) {
        _boundingSphereMs._radius = std::max(_boundingSphereMs._radius, glm::distance(vertex.position, _boundingSphereMs._center));
    }
    _boundingSphereMs._radius *= 1.001f; // Add a small safety margin
}

}
