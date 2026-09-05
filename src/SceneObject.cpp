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

    PushConstants push = _material->build_push_constants();
    push.set(HASH("model"), transform());
    draw_mesh(
        _material->program(),
        _material->raster_state(),
        _material->pass_resources(),
        push,
        _mesh->vertex_buffer(),
        _mesh->index_buffer(),
        _mesh->index_count()
    );
}

const Material& SceneObject::material() const {
    DEBUG_ASSERT(_material);
    return *_material;
}

void SceneObject::set_transform(const glm::mat4& tr) {
    _transform = tr;
}

const glm::mat4& SceneObject::transform() const {
    return _transform;
}

}
