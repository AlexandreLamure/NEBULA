#include "Program.h"

#include "VkContext.h"
#include "Vertex.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace OM3D {

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

static bool file_exists(const std::string& path) {
    if(FILE* file = std::fopen(path.c_str(), "rb")) {
        std::fclose(file);
        return true;
    }
    return false;
}

static std::string spirv_path(const std::string& file, Span<const std::string> defines) {
    const std::string name = module_name_from_file(file);
    std::string with_defs = name;
    for(const std::string& def : defines) {
        with_defs += "_" + def;
    }
    const std::string preferred = std::string(shader_path) + with_defs + ".spv";
    if(file_exists(preferred)) {
        return preferred;
    }
    return std::string(shader_path) + name + ".spv";
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

static u32 current_pipeline_key() {
    return u32(ctx().alpha_blend)
         | (u32(ctx().has_vertex_input) << 1)
         | (u32(ctx().rendering_color_format) << 4)
         | (u32(ctx().rendering_depth_format) << 16);
}

static VkPipeline create_graphics_pipeline(VkShaderModule vert, VkShaderModule frag, bool alpha_blend, bool vertex_input) {
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

    // Fixed Vertex layout (locations 0–4). This is pipeline state, not per-draw like glVertexAttribPointer.
    // Fullscreen pipelines pass vertex_input = false and use SV_VertexID instead.
    VkVertexInputBindingDescription binding{
        .binding = 0,
        .stride = u32(sizeof(Vertex)),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, position))},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, normal))},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, u32(offsetof(Vertex, uv))},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, u32(offsetof(Vertex, tangent_bitangent_sign))},
        {4, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, color))},
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    if(vertex_input) {
        vertex_input_state.vertexBindingDescriptionCount = 1;
        vertex_input_state.pVertexBindingDescriptions = &binding;
        vertex_input_state.vertexAttributeDescriptionCount = 5;
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
    std::swap(_push, other._push);
    std::swap(_is_compute, other._is_compute);
}

Program::Program(const std::string& frag, const std::string& vert, Span<const std::string> defines) {
    load_graphics(frag, vert, defines);
}

Program::Program(const std::string& comp, Span<const std::string> defines) : _is_compute(true) {
    load_compute(comp, defines);
}

void Program::load_graphics(const std::string& frag, const std::string& vert, Span<const std::string> defines) {
    _vert_module = create_shader_module(load_spirv(spirv_path(vert, defines)));
    _frag_module = create_shader_module(load_spirv(spirv_path(frag, defines)));
}

void Program::load_compute(const std::string& comp, Span<const std::string> defines) {
    _comp_module = create_shader_module(load_spirv(spirv_path(comp, defines)));

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
    if(ctx().bound_program == this) {
        ctx().bound_program = nullptr;
    }
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

VkPipeline Program::get_or_create_pipeline() const {
    const u32 key = current_pipeline_key();
    for(const CachedPipeline& cached : _pipelines) {
        if(cached.key == key) {
            return cached.pipeline;
        }
    }

    const VkPipeline pipeline = create_graphics_pipeline(
        _vert_module,
        _frag_module,
        ctx().alpha_blend,
        ctx().has_vertex_input
    );
    _pipelines.push_back({key, pipeline});
    return pipeline;
}

void Program::bind() const {
    ctx().bound_program = this;

    if(!vk_is_recording()) {
        return;
    }

    const VkCommandBuffer cmd = vk_command_buffer();
    if(_is_compute) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _compute_pipeline);
    } else {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, get_or_create_pipeline());
        // Dynamic state is undefined after a pipeline bind; re-apply Material's sticky raster state.
        vkCmdSetCullMode(cmd, ctx().cull_mode);
        vkCmdSetDepthTestEnable(cmd, ctx().depth_test_enable ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(cmd, ctx().depth_compare_op);
    }

    flush_push_constants();
}

void Program::flush_push_constants() const {
    if(!vk_is_recording() || !ctx().pipeline_layout) {
        return;
    }
    vkCmdPushConstants(
        vk_command_buffer(),
        ctx().pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(_push),
        &_push
    );
}

bool Program::is_compute() const {
    return _is_compute;
}

std::shared_ptr<Program> Program::from_file(const std::string& comp, Span<const std::string> defines) {
    static std::unordered_map<std::vector<std::string>, std::weak_ptr<Program>, CollectionHasher<std::vector<std::string>>> loaded;

    std::vector<std::string> key(defines.begin(), defines.end());
    key.emplace_back(comp);

    auto& weak_program = loaded[key];
    auto program = weak_program.lock();
    if(!program) {
        program = std::make_shared<Program>(comp, defines);
        weak_program = program;
    }
    return program;
}

std::shared_ptr<Program> Program::from_files(const std::string& frag, const std::string& vert, Span<const std::string> defines) {
    static std::unordered_map<std::vector<std::string>, std::weak_ptr<Program>, CollectionHasher<std::vector<std::string>>> loaded;

    std::vector<std::string> key(defines.begin(), defines.end());
    key.emplace_back(frag);
    key.emplace_back(vert);

    auto& weak_program = loaded[key];
    auto program = weak_program.lock();
    if(!program) {
        program = std::make_shared<Program>(frag, vert, defines);
        weak_program = program;
    }
    return program;
}

void Program::write_uniform(u32 name_hash, const void* data, u32 size) {
    const int off = uniform_offset(name_hash);
    if(off < 0) {
        return;
    }
    DEBUG_ASSERT(u32(off) + size <= sizeof(_push));
    std::memcpy(reinterpret_cast<u8*>(&_push) + off, data, size);
}

}
