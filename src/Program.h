#ifndef PROGRAM_H
#define PROGRAM_H

#include <graphics.h>

#include <volk.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat2x2.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>

#include <memory>
#include <vector>
#include <variant>

namespace nebula {

using UniformValue = std::variant<
    u32,
    float,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    glm::mat2,
    glm::mat3,
    glm::mat4
>;

// CPU mirror of the Slang `PushConstants` struct in structs.slang.
struct PushConstants {
    glm::mat4 model = glm::mat4(1.0f);
    glm::vec3 baseColorFactor = glm::vec3(1.0f);
    float alphaCutoff = 0.0f;
    glm::vec2 metalRoughFactor = glm::vec2(1.0f);
    glm::vec2 viewportSize = {};
    glm::vec3 emissiveFactor = {};
    float intensity = 1.0f;
    float exposure = 1.0f;

    void set(u32 nameHash, const UniformValue& value);
    template<typename T>
    void set(u32 nameHash, const T& value) {
        write(nameHash, &value, sizeof(value));
    }

    private:
        void write(u32 nameHash, const void* data, u32 size);
};

class Program : NonCopyable {

    public:
        Program() = default;
        Program(Program&& other);
        Program& operator=(Program&& other);

        Program(const std::string& vert, const std::string& frag);
        Program(const std::string& comp);
        ~Program();

        // Bind graphics pipeline variant + dynamic raster state + push constants.
        void bindGraphics(const RasterState& raster, VertexLayout layout, const PushConstants& push) const;
        void bindCompute() const;

        bool isCompute() const;

        static std::shared_ptr<Program> fromFile(const std::string& comp);
        static std::shared_ptr<Program> fromFiles(const std::string& vert, const std::string& frag);

    private:
        void swap(Program& other);
        void destroy();

        void loadGraphics(const std::string& vert, const std::string& frag);
        void loadCompute(const std::string& comp);

        VkPipeline getOrCreatePipeline(bool alphaBlend, VertexLayout layout) const;

        VkShaderModule _vertModule = VK_NULL_HANDLE;
        VkShaderModule _fragModule = VK_NULL_HANDLE;
        VkShaderModule _compModule = VK_NULL_HANDLE;
        VkPipeline _computePipeline = VK_NULL_HANDLE;

        struct CachedPipeline {
            u32 key = 0;
            VkPipeline pipeline = VK_NULL_HANDLE;
        };
        mutable std::vector<CachedPipeline> _pipelines;

        bool _isCompute = false;

};

}

#endif // PROGRAM_H
