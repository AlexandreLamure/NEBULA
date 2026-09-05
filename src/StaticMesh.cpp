#include "StaticMesh.h"

namespace nebula {

StaticMesh::StaticMesh(const MeshData& data) :
    _vertexBuffer(data.vertices),
    _indexBuffer(data.indices) {
}

}
