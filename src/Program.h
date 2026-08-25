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

namespace OM3D {

// CPU mirror of the Slang `PushConstants` struct in structs.slang.
// `set_uniform` writes here; `vkCmdPushConstants` uploads the blob at bind/draw.
struct PushConstants {
    glm::mat4 model = glm::mat4(1.0f);
    glm::vec3 base_color_factor = glm::vec3(1.0f);
    float alpha_cutoff = 0.0f;
    glm::vec2 metal_rough_factor = glm::vec2(1.0f);
    glm::vec2 viewport_size = {};
    glm::vec3 emissive_factor = {};
    float intensity = 1.0f;
    float exposure = 1.0f;
};

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

class Program : NonCopyable {

    public:
        Program() = default;
        Program(Program&& other);
        Program& operator=(Program&& other);

        Program(const std::string& frag, const std::string& vert, Span<const std::string> defines = {});
        Program(const std::string& comp, Span<const std::string> defines = {});
        ~Program();

        void bind() const;
        void flush_push_constants() const;

        bool is_compute() const;

        static std::shared_ptr<Program> from_file(const std::string& comp, Span<const std::string> defines = {});
        static std::shared_ptr<Program> from_files(const std::string& frag, const std::string& vert, Span<const std::string> defines = {});

        template<typename T>
        void set_uniform(std::string_view name, const T& value) {
            write_uniform(str_hash(name), &value, sizeof(value));
        }

    private:
        void swap(Program& other);
        void destroy();

        void load_graphics(const std::string& frag, const std::string& vert, Span<const std::string> defines);
        void load_compute(const std::string& comp, Span<const std::string> defines);

        VkPipeline get_or_create_pipeline() const;
        void write_uniform(u32 name_hash, const void* data, u32 size);

        VkShaderModule _vert_module = VK_NULL_HANDLE;
        VkShaderModule _frag_module = VK_NULL_HANDLE;
        VkShaderModule _comp_module = VK_NULL_HANDLE;
        VkPipeline _compute_pipeline = VK_NULL_HANDLE;

        struct CachedPipeline {
            u32 key = 0;
            VkPipeline pipeline = VK_NULL_HANDLE;
        };
        mutable std::vector<CachedPipeline> _pipelines;

        PushConstants _push;
        bool _is_compute = false;

};

}

#endif // PROGRAM_H
