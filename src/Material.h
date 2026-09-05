#ifndef MATERIAL_H
#define MATERIAL_H

#include <Program.h>
#include <Texture.h>

#include <memory>
#include <vector>

namespace NEBULA {

enum class BlendMode {
    None,
    Alpha,
};

enum class DepthTestMode {
    Standard,
    Reversed,
    Equal,
    None
};

class Material {

    public:
        Material();

        void set_program(std::shared_ptr<Program> prog);
        void set_blend_mode(BlendMode blend);
        void set_depth_test_mode(DepthTestMode depth);
        void set_double_sided(bool double_sided);
        void set_texture(u32 slot, std::shared_ptr<Texture> tex);

        bool is_opaque() const;

        void set_stored_uniform(u32 name_hash, UniformValue value);

        const Program& program() const;
        RasterState raster_state() const;
        PassResources pass_resources() const;
        PushConstants build_push_constants() const;

        static Material textured_pbr_material(bool alpha_test = false);

    private:
        std::shared_ptr<Program> _program;
        std::vector<std::pair<u32, std::shared_ptr<Texture>>> _textures;
        std::vector<std::pair<u32, UniformValue>> _uniforms;

        BlendMode _blend_mode = BlendMode::None;
        DepthTestMode _depth_test_mode = DepthTestMode::Standard;
        bool _double_sided = false;
};

}

#endif // MATERIAL_H
