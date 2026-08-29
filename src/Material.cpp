#include "Material.h"

#include "VkContext.h"

#include <algorithm>

namespace OM3D {

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

// Material::bind() = OpenGL-style sticky intent (blend, depth, cull, textures, uniforms)
// Program::bind() = bind pipeline, then vkCmdSetCullMode / DepthTestEnable / DepthCompareOp from that intent
// TODO: change that OpenGL styicky state machine to a more Vulkan-like approach.
void Material::bind() const {
    GraphicsContext& c = ctx();

    // Blend is not dynamic in Vulkan 1.3: Program picks a pipeline variant from this flag.
    c.alpha_blend = (_blend_mode == BlendMode::Alpha);

    // Depth/cull *are* dynamic. Program::bind() issues the vkCmdSet* after the pipeline is bound.
    c.cull_mode = _double_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

    switch(_depth_test_mode) {
        case DepthTestMode::None:
            c.depth_test_enable = false;
        break;

        case DepthTestMode::Equal:
            c.depth_test_enable = true;
            c.depth_compare_op = VK_COMPARE_OP_EQUAL;
        break;

        case DepthTestMode::Standard:
            c.depth_test_enable = true;
            // Reverse-Z: nearer fragments have *greater* depth.
            c.depth_compare_op = VK_COMPARE_OP_GREATER_OR_EQUAL;
        break;

        case DepthTestMode::Reversed:
            c.depth_test_enable = true;
            c.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    }

    for(const auto& texture : _textures) {
        texture.second->bind(texture.first);
    }

    for(const auto& [h, v] : _uniforms) {
        _program->set_uniform(h, v);
    }

    _program->bind();
}

Material Material::textured_pbr_material(bool alpha_test) {
    Material material;

    std::vector<std::string> defines;
    if(alpha_test) {
        defines.emplace_back("ALPHA_TEST");
    }

    material._program = Program::from_files("lit.frag", "basic.vert", defines);

    material.set_texture(0u, default_white_texture());
    material.set_texture(1u, default_normal_texture());
    material.set_texture(2u, default_metal_rough_texture());
    material.set_texture(3u, default_white_texture());

    return material;
}


}
