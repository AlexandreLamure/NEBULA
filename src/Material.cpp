#include "Material.h"

#include <algorithm>

namespace nebula {

Material::Material() {
}

void Material::set_program(std::shared_ptr<Program> prog) {
    _program = std::move(prog);
}

void Material::set_blend_mode(BlendMode blend) {
    _blend_mode = blend;
}

void Material::set_depth_test_mode(DepthTestMode depth) {
    _depth_test_mode = depth;
}

void Material::set_double_sided(bool double_sided) {
    _double_sided = double_sided;
}

void Material::set_texture(u32 slot, std::shared_ptr<Texture> tex) {
    if(const auto it = std::find_if(_textures.begin(), _textures.end(), [&](const auto& t) { return t.first == slot; }); it != _textures.end()) {
        it->second = std::move(tex);
    } else {
        _textures.emplace_back(slot, std::move(tex));
    }
}

bool Material::is_opaque() const {
    return _blend_mode == BlendMode::None;
}

void Material::set_stored_uniform(u32 name_hash, UniformValue value) {
    for(auto& [h, v] : _uniforms) {
        if(h == name_hash) {
            v = value;
            return;
        }
    }
    _uniforms.emplace_back(name_hash, std::move(value));
}

const Program& Material::program() const {
    DEBUG_ASSERT(_program);
    return *_program;
}

RasterState Material::raster_state() const {
    RasterState raster;
    raster.alpha_blend = (_blend_mode == BlendMode::Alpha);
    raster.cull_mode = _double_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

    switch(_depth_test_mode) {
        case DepthTestMode::None:
            raster.depth_test_enable = false;
            break;

        case DepthTestMode::Equal:
            raster.depth_test_enable = true;
            raster.depth_compare_op = VK_COMPARE_OP_EQUAL;
            break;

        case DepthTestMode::Standard:
            raster.depth_test_enable = true;
            // Reverse-Z: nearer fragments have *greater* depth.
            raster.depth_compare_op = VK_COMPARE_OP_GREATER_OR_EQUAL;
            break;

        case DepthTestMode::Reversed:
            raster.depth_test_enable = true;
            raster.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
    }

    return raster;
}

PassResources Material::pass_resources() const {
    PassResources pass{};
    for(const auto& [slot, tex] : _textures) {
        if(slot < pass_texture_slot_count && tex) {
            pass.textures[slot] = tex.get();
        }
    }
    return pass;
}

PushConstants Material::build_push_constants() const {
    PushConstants push;
    for(const auto& [h, v] : _uniforms) {
        push.set(h, v);
    }
    return push;
}

Material Material::textured_pbr_material(bool alpha_test) {
    Material material;

    const char* frag = alpha_test ? "lit_ALPHA_TEST.slang" : "lit.slang";
    material._program = Program::from_files("basic.slang", frag);

    material.set_texture(0u, default_white_texture());
    material.set_texture(1u, default_normal_texture());
    material.set_texture(2u, default_metal_rough_texture());
    material.set_texture(3u, default_white_texture());

    return material;
}

}
