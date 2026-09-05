#include "SceneObject.h"

#include "graphics.h"

#include <glm/gtc/matrix_transform.hpp>

namespace nebula {

SceneObject::SceneObject(std::shared_ptr<StaticMesh> mesh, std::shared_ptr<Material> material) :
    _mesh(std::move(mesh)),
    _material(std::move(material)) {
}

void SceneObject::render() const {
    if(!_material || !_mesh) {
        return;
    }

    PushConstants push = _material->buildPushConstants();
    push.set(HASH("model"), transform());
    drawMesh(
        _material->program(),
        _material->rasterState(),
        _material->passResources(),
        push,
        _mesh->vertexBuffer(),
        _mesh->indexBuffer(),
        _mesh->indexCount()
    );
}

const Material& SceneObject::material() const {
    DEBUG_ASSERT(_material);
    return *_material;
}

void SceneObject::setTransform(const glm::mat4& tr) {
    _transform = tr;
}

const glm::mat4& SceneObject::transform() const {
    return _transform;
}

}
