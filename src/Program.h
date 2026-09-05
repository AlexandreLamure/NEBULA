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

namespace NEBULA {

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
    glm::vec3 base_color_factor = glm::vec3(1.0f);
    float alpha_cutoff = 0.0f;
    glm::vec2 metal_rough_factor = glm::vec2(1.0f);
    glm::vec2 viewport_size = {};
    glm::vec3 emissive_factor = {};
    float intensity = 1.0f;
    float exposure = 1.0f;

    void set(u32 name_hash, const UniformValue& value);
    template<typename T>
    void set(u32 name_hash, const T& value) {
        write(name_hash, &value, sizeof(value));
    }

    private:
        void write(u32 name_hash, const void* data, u32 size);
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
        void bind_graphics(const RasterState& raster, VertexLayout layout, const PushConstants& push) const;
        void bind_compute() const;

        bool is_compute() const;

        static std::shared_ptr<Program> from_file(const std::string& comp);
        static std::shared_ptr<Program> from_files(const std::string& vert, const std::string& frag);

    private:
        void swap(Program& other);
        void destroy();

        void load_graphics(const std::string& vert, const std::string& frag);
        void load_compute(const std::string& comp);

        VkPipeline get_or_create_pipeline(bool alpha_blend, VertexLayout layout) const;

        VkShaderModule _vert_module = VK_NULL_HANDLE;
        VkShaderModule _frag_module = VK_NULL_HANDLE;
        VkShaderModule _comp_module = VK_NULL_HANDLE;
        VkPipeline _compute_pipeline = VK_NULL_HANDLE;

        struct CachedPipeline {
            u32 key = 0;
            VkPipeline pipeline = VK_NULL_HANDLE;
        };
        mutable std::vector<CachedPipeline> _pipelines;

        bool _is_compute = false;

};

}

#endif // PROGRAM_H
