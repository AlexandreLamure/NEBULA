#include "Scene.h"

#include "VkContext.h"
#include <TypedBuffer.h>

#include <shaderStructs.h>

namespace nebula {

Scene::Scene() {
    _skyMaterial.setProgram(Program::fromFiles("screen.slang", "sky.slang"));
    _skyMaterial.setDepthTestMode(DepthTestMode::None);

    _envmap = std::make_shared<Texture>(Texture::emptyCubemap(4, ImageFormat::RGBA8_UNORM));
}

void Scene::addObject(SceneObject obj) {
    _objects.emplace_back(std::move(obj));
}

void Scene::addLight(PointLight obj) {
    _pointLights.emplace_back(std::move(obj));
}

Span<const SceneObject> Scene::objects() const {
    return _objects;
}

Span<const PointLight> Scene::pointLights() const {
    return _pointLights;
}

Camera& Scene::camera() {
    return _camera;
}

const Camera& Scene::camera() const {
    return _camera;
}

void Scene::setEnvmap(std::shared_ptr<Texture> env) {
    _envmap = std::move(env);
}

void Scene::setIblIntensity(float intensity) {
    _iblIntensity = intensity;
}

void Scene::setSun(float altitude, float azimuth, glm::vec3 color) {
    // Convert from degrees to radians
    const float alt = glm::radians(altitude);
    const float azi = glm::radians(azimuth);
    // Convert from polar to cartesian
    _sunDirection = glm::vec3(sin(azi) * cos(alt), sin(alt), cos(azi) * cos(alt));
    _sunColor = color;
}

void Scene::render() const {
    // These TypedBuffers are stack locals destroyed at the end of render().
    // GPU work is often one frame behind, so ~ByteBuffer enqueues the VkBuffer
    // and the deletion queue frees it after this frame's fence.

    TypedBuffer<shader::FrameData> buffer(nullptr, 1);
    {
        auto mapping = buffer.map(AccessType::WriteOnly);
        mapping[0].camera.viewProj = _camera.viewProjMatrix();
        mapping[0].camera.invViewProj = glm::inverse(_camera.viewProjMatrix());
        mapping[0].camera.position = _camera.position();
        mapping[0].pointLightCount = u32(_pointLights.size());
        mapping[0].sunColor = _sunColor;
        mapping[0].sunDir = glm::normalize(_sunDirection);
        mapping[0].iblIntensity = _iblIntensity;
    }

    TypedBuffer<shader::PointLight> lightBuffer(nullptr, std::max(_pointLights.size(), size_t(1)));
    {
        auto mapping = lightBuffer.map(AccessType::WriteOnly);
        for(size_t i = 0; i != _pointLights.size(); ++i) {
            const auto& light = _pointLights[i];
            mapping[i] = {
                light.position(),
                light.radius(),
                light.color(),
                0.0f
            };
        }
    }

    DEBUG_ASSERT(_envmap && !_envmap->isNull());
    bindFrame({
        .ubo = buffer.vkBuffer(),
        .uboSize = buffer.byteSize(),
        .lights = lightBuffer.vkBuffer(),
        .lightsSize = lightBuffer.byteSize(),
        .env = _envmap.get(),
        .brdf = &brdfLut(),
    });

    // Sky: no depth, no cull, intensity from IBL.
    {
        PushConstants push = _skyMaterial.buildPushConstants();
        push.set(HASH("intensity"), _iblIntensity);
        RasterState raster = _skyMaterial.rasterState();
        raster.cullMode = VK_CULL_MODE_NONE;
        drawFullscreen(_skyMaterial.program(), raster, _skyMaterial.passResources(), push);
    }

    const Frustum frustum = _camera.buildFrustum();

    // Opaque first, then transparent.
    for(const SceneObject& obj : _objects) {
        if(obj.material().isOpaque()) {
            if(frustumSphereIntersection(frustum, obj.computeBoundingSphereWs())) {
                obj.render();
            }
        }
    }
    for(const SceneObject& obj : _objects) {
        if(!obj.material().isOpaque()) {
            if(frustumSphereIntersection(frustum, obj.computeBoundingSphereWs())) {
                obj.render();
            }
        }
    }
}

}
