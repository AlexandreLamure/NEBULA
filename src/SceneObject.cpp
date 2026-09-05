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

const Sphere SceneObject::computeBoundingSphereWs() const {
    const Sphere& boundingSphereMs = _mesh->boundingSphereMs();
    const glm::vec3 centerWs = transform() * glm::vec4(boundingSphereMs._center, 1.0f);
    const glm::vec3 scaleWs = glm::vec3(glm::length(transform()[0]), glm::length(transform()[1]), glm::length(transform()[2]));
    const float radiusWs = boundingSphereMs._radius * std::max(std::max(scaleWs.x, scaleWs.y), scaleWs.z);
    return Sphere(centerWs, radiusWs);
}

}
