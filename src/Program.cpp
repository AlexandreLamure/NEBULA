#include "Program.h"

#include "VkContext.h"
#include "Vertex.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace NEBULA {

static_assert(offsetof(PushConstants, model) == 0);
static_assert(offsetof(PushConstants, base_color_factor) == 64);
static_assert(offsetof(PushConstants, alpha_cutoff) == 76);
static_assert(offsetof(PushConstants, metal_rough_factor) == 80);
static_assert(offsetof(PushConstants, viewport_size) == 88);
static_assert(offsetof(PushConstants, emissive_factor) == 96);
static_assert(offsetof(PushConstants, intensity) == 108);
static_assert(offsetof(PushConstants, exposure) == 112);
static_assert(sizeof(PushConstants) <= 128);

static std::string module_name_from_file(const std::string& file) {
    std::string name = file;
    const auto slash = name.find_last_of("/\\");
    if(slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    const auto dot = name.rfind('.');
    if(dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    return name;
}

// Maps HASH("uniform_name") onto a byte offset in the PushConstants blob (Vulkan has no named uniforms).
static int uniform_offset(u32 hash) {
    switch(hash) {
        case str_hash("model"): return int(offsetof(PushConstants, model));
        case str_hash("base_color_factor"): return int(offsetof(PushConstants, base_color_factor));
        case str_hash("alpha_cutoff"): return int(offsetof(PushConstants, alpha_cutoff));
        case str_hash("metal_rough_factor"): return int(offsetof(PushConstants, metal_rough_factor));
        case str_hash("viewport_size"): return int(offsetof(PushConstants, viewport_size));
        case str_hash("emissive_factor"): return int(offsetof(PushConstants, emissive_factor));
        case str_hash("intensity"): return int(offsetof(PushConstants, intensity));
        case str_hash("exposure"): return int(offsetof(PushConstants, exposure));
        default: return -1;
    }
}

void PushConstants::write(u32 name_hash, const void* data, u32 size) {
    const int off = uniform_offset(name_hash);
    if(off < 0) {
        return;
    }
    DEBUG_ASSERT(u32(off) + size <= sizeof(PushConstants));
    std::memcpy(reinterpret_cast<u8*>(this) + off, data, size);
}

void PushConstants::set(u32 name_hash, const UniformValue& value) {
    std::visit([&](const auto& v) {
        write(name_hash, &v, sizeof(v));
    }, value);
}

static bool file_exists(const std::string& path) {
    if(FILE* file = std::fopen(path.c_str(), "rb")) {
        std::fclose(file);
        return true;
    }
    return false;
}

static std::string spirv_path(const std::string& file) {
    const std::string path = std::string(NEBULA_SHADER_PATH) + module_name_from_file(file) + ".spv";
    ALWAYS_ASSERT(file_exists(path), ("Unable to find SPIR-V: \"" + path + '"').c_str());
    return path;
}

static std::vector<u32> load_spirv(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    ALWAYS_ASSERT(file, ("Unable to read SPIR-V: \"" + path + '"').c_str());
    DEFER(std::fclose(file));

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    ALWAYS_ASSERT(size > 0 && size % long(sizeof(u32)) == 0, "Invalid SPIR-V");
    std::rewind(file);

    std::vector<u32> words(size / long(sizeof(u32)));
    ALWAYS_ASSERT(std::fread(words.data(), 1, size, file) == size_t(size), "Unable to read SPIR-V");
    return words;
}

static VkShaderModule create_shader_module(const std::vector<u32>& spirv) {
    const VkShaderModuleCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size() * sizeof(u32),
        .pCode = spirv.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    vk_check(vkCreateShaderModule(vk_device(), &ci, nullptr, &module));
    return module;
}

// Packs blend, vertex layout, and attachment formats into a u32 used to cache pipeline variants.
static u32 pipeline_key(bool alpha_blend, VertexLayout vertex_layout) {
    return u32(alpha_blend)
         | (u32(vertex_layout) << 1)
         | (u32(ctx().rendering_color_format) << 4)
         | (u32(ctx().rendering_depth_format) << 16);
}

static VkPipeline create_graphics_pipeline(VkShaderModule vert, VkShaderModule frag, bool alpha_blend, VertexLayout vertex_layout) {
    const VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "vertex_main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "fragment_main",
        },
    };

    // Vertex layout is pipeline state, not rebound per draw.
    // Mesh: Vertex.h locations 0–4. ImGui: ImDrawVert. None: SV_VertexID, no buffer.
    VkVertexInputBindingDescription binding{
        .binding = 0,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[5] = {};
    u32 attr_count = 0;

    if(vertex_layout == VertexLayout::Mesh) {
        binding.stride = u32(sizeof(Vertex));
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, position))};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, normal))};
        attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, u32(offsetof(Vertex, uv))};
        attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, u32(offsetof(Vertex, tangent_bitangent_sign))};
        attrs[4] = {4, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, color))};
        attr_count = 5;
    } else if(vertex_layout == VertexLayout::ImGui) {
        // Packed like ImDrawVert: float2 pos, float2 uv, RGBA8 unorm (20 bytes).
        binding.stride = 20;
        attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
        attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 8};
        attrs[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16};
        attr_count = 3;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    if(attr_count) {
        vertex_input_state.vertexBindingDescriptionCount = 1;
        vertex_input_state.pVertexBindingDescriptions = &binding;
        vertex_input_state.vertexAttributeDescriptionCount = attr_count;
        vertex_input_state.pVertexAttributeDescriptions = attrs;
    }

    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const VkPipelineRasterizationStateCreateInfo raster{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    const VkPipelineDepthStencilStateCreateInfo depth{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
    };

    VkPipelineColorBlendAttachmentState blend_attachment{
        .blendEnable = alpha_blend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo color_blend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    const VkPipelineDynamicStateCreateInfo dynamic{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 5,
        .pDynamicStates = dynamic_states,
    };

    const VkPipelineRenderingCreateInfo rendering{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &ctx().rendering_color_format,
        .depthAttachmentFormat = ctx().rendering_depth_format,
    };

    const VkGraphicsPipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input_state,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic,
        .layout = ctx().pipeline_layout,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    vk_check(vkCreateGraphicsPipelines(vk_device(), VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipeline));
    return pipeline;
}


Program::Program(Program&& other) {
    swap(other);
}

Program& Program::operator=(Program&& other) {
    swap(other);
    return *this;
}

void Program::swap(Program& other) {
    std::swap(_vert_module, other._vert_module);
    std::swap(_frag_module, other._frag_module);
    std::swap(_comp_module, other._comp_module);
    std::swap(_compute_pipeline, other._compute_pipeline);
    std::swap(_pipelines, other._pipelines);
    std::swap(_is_compute, other._is_compute);
}

Program::Program(const std::string& vert, const std::string& frag) {
    load_graphics(vert, frag);
}

Program::Program(const std::string& comp) : _is_compute(true) {
    load_compute(comp);
}

void Program::load_graphics(const std::string& vert, const std::string& frag) {
    _vert_module = create_shader_module(load_spirv(spirv_path(vert)));
    _frag_module = create_shader_module(load_spirv(spirv_path(frag)));
}

void Program::load_compute(const std::string& comp) {
    _comp_module = create_shader_module(load_spirv(spirv_path(comp)));

    const VkComputePipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = _comp_module,
            .pName = "compute_main",
        },
        .layout = ctx().pipeline_layout,
    };
    vk_check(vkCreateComputePipelines(vk_device(), VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &_compute_pipeline));
}

void Program::destroy() {
    if(!vk_device()) {
        return;
    }

    // Enqueue in creation order so reverse flush destroys pipelines before modules.
    defer_destroy(_vert_module);
    defer_destroy(_frag_module);
    defer_destroy(_comp_module);
    defer_destroy(_compute_pipeline);
    for(CachedPipeline& cached : _pipelines) {
        defer_destroy(cached.pipeline);
    }

    _vert_module = VK_NULL_HANDLE;
    _frag_module = VK_NULL_HANDLE;
    _comp_module = VK_NULL_HANDLE;
    _compute_pipeline = VK_NULL_HANDLE;
    _pipelines.clear();
}

Program::~Program() {
    destroy();
}

VkPipeline Program::get_or_create_pipeline(bool alpha_blend, VertexLayout layout) const {
    const u32 key = pipeline_key(alpha_blend, layout);
    for(const CachedPipeline& cached : _pipelines) {
        if(cached.key == key) {
            return cached.pipeline;
        }
    }

    const VkPipeline pipeline = create_graphics_pipeline(
        _vert_module,
        _frag_module,
        alpha_blend,
        layout
    );
    _pipelines.push_back({key, pipeline});
    return pipeline;
}

void Program::bind_graphics(const RasterState& raster, VertexLayout layout, const PushConstants& push) const {
    DEBUG_ASSERT(!_is_compute);
    if(!vk_is_recording()) {
        return;
    }

    const VkCommandBuffer cmd = vk_command_buffer();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, get_or_create_pipeline(raster.alpha_blend, layout));
    vkCmdSetCullMode(cmd, raster.cull_mode);
    vkCmdSetDepthTestEnable(cmd, raster.depth_test_enable ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, raster.depth_compare_op);

    if(ctx().pipeline_layout) {
        vkCmdPushConstants(
            cmd,
            ctx().pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(push),
            &push
        );
    }
}

void Program::bind_compute() const {
    DEBUG_ASSERT(_is_compute);
    if(!vk_is_recording()) {
        return;
    }
    vkCmdBindPipeline(vk_command_buffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _compute_pipeline);
}

bool Program::is_compute() const {
    return _is_compute;
}

std::shared_ptr<Program> Program::from_file(const std::string& comp) {
    static std::unordered_map<std::string, std::weak_ptr<Program>> loaded;

    auto& weak_program = loaded[comp];
    auto program = weak_program.lock();
    if(!program) {
        program = std::make_shared<Program>(comp);
        weak_program = program;
    }
    return program;
}

std::shared_ptr<Program> Program::from_files(const std::string& vert, const std::string& frag) {
    static std::unordered_map<std::string, std::weak_ptr<Program>> loaded;

    auto& weak_program = loaded[vert + '\n' + frag];
    auto program = weak_program.lock();
    if(!program) {
        program = std::make_shared<Program>(vert, frag);
        weak_program = program;
    }
    return program;
}

}
