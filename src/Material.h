#ifndef MATERIAL_H
#define MATERIAL_H

#include <Program.h>
#include <Texture.h>

#include <memory>
#include <vector>

namespace nebula {

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

        void setProgram(std::shared_ptr<Program> prog);
        void setBlendMode(BlendMode blend);
        void setDepthTestMode(DepthTestMode depth);
        void setDoubleSided(bool doubleSided);
        void setTexture(u32 slot, std::shared_ptr<Texture> tex);

        bool isOpaque() const;

        void setStoredUniform(u32 nameHash, UniformValue value);

        const Program& program() const;
        RasterState rasterState() const;
        PassResources passResources() const;
        PushConstants buildPushConstants() const;

        static Material texturedPbrMaterial(bool alphaTest = false);

    private:
        std::shared_ptr<Program> _program;
        std::vector<std::pair<u32, std::shared_ptr<Texture>>> _textures;
        std::vector<std::pair<u32, UniformValue>> _uniforms;

        BlendMode _blendMode = BlendMode::None;
        DepthTestMode _depthTestMode = DepthTestMode::Standard;
        bool _doubleSided = false;
};

}

#endif // MATERIAL_H
