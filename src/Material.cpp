#include "Material.h"

#include <algorithm>

namespace nebula {

Material::Material() {
}

void Material::setProgram(std::shared_ptr<Program> prog) {
    _program = std::move(prog);
}

void Material::setBlendMode(BlendMode blend) {
    _blendMode = blend;
}

void Material::setDepthTestMode(DepthTestMode depth) {
    _depthTestMode = depth;
}

void Material::setDoubleSided(bool doubleSided) {
    _doubleSided = doubleSided;
}

void Material::setTexture(u32 slot, std::shared_ptr<Texture> tex) {
    if(const auto it = std::find_if(_textures.begin(), _textures.end(), [&](const auto& t) { return t.first == slot; }); it != _textures.end()) {
        it->second = std::move(tex);
    } else {
        _textures.emplace_back(slot, std::move(tex));
    }
}

bool Material::isOpaque() const {
    return _blendMode == BlendMode::None;
}

void Material::setStoredUniform(u32 nameHash, UniformValue value) {
    for(auto& [h, v] : _uniforms) {
        if(h == nameHash) {
            v = value;
            return;
        }
    }
    _uniforms.emplace_back(nameHash, std::move(value));
}

const Program& Material::program() const {
    DEBUG_ASSERT(_program);
    return *_program;
}

RasterState Material::rasterState() const {
    RasterState raster;
    raster.alphaBlend = (_blendMode == BlendMode::Alpha);
    raster.cullMode = _doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

    switch(_depthTestMode) {
        case DepthTestMode::None:
            raster.depthTestEnable = false;
            break;

        case DepthTestMode::Equal:
            raster.depthTestEnable = true;
            raster.depthCompareOp = VK_COMPARE_OP_EQUAL;
            break;

        case DepthTestMode::Standard:
            raster.depthTestEnable = true;
            // Reverse-Z: nearer fragments have *greater* depth.
            raster.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
            break;

        case DepthTestMode::Reversed:
            raster.depthTestEnable = true;
            raster.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
    }

    return raster;
}

PassResources Material::passResources() const {
    PassResources pass{};
    for(const auto& [slot, tex] : _textures) {
        if(slot < passTextureSlotCount && tex) {
            pass.textures[slot] = tex.get();
        }
    }
    return pass;
}

PushConstants Material::buildPushConstants() const {
    PushConstants push;
    for(const auto& [h, v] : _uniforms) {
        push.set(h, v);
    }
    return push;
}

Material Material::texturedPbrMaterial(bool alphaTest) {
    Material material;

    const char* frag = alphaTest ? "lit_ALPHA_TEST.slang" : "lit.slang";
    material._program = Program::fromFiles("basic.slang", frag);

    material.setTexture(0u, defaultWhiteTexture());
    material.setTexture(1u, defaultNormalTexture());
    material.setTexture(2u, defaultMetalRoughTexture());
    material.setTexture(3u, defaultWhiteTexture());

    return material;
}

}
