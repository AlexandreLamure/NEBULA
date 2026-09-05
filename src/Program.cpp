#include "Program.h"

#include "VkContext.h"
#include "Vertex.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace nebula {

static_assert(offsetof(PushConstants, model) == 0);
static_assert(offsetof(PushConstants, baseColorFactor) == 64);
static_assert(offsetof(PushConstants, alphaCutoff) == 76);
static_assert(offsetof(PushConstants, metalRoughFactor) == 80);
static_assert(offsetof(PushConstants, viewportSize) == 88);
static_assert(offsetof(PushConstants, emissiveFactor) == 96);
static_assert(offsetof(PushConstants, intensity) == 108);
static_assert(offsetof(PushConstants, exposure) == 112);
static_assert(sizeof(PushConstants) <= 128);

static std::string moduleNameFromFile(const std::string& file) {
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

// Maps HASH("uniformName") onto a byte offset in the PushConstants blob (Vulkan has no named uniforms).
static int uniformOffset(u32 hash) {
    switch(hash) {
        case strHash("model"): return int(offsetof(PushConstants, model));
        case strHash("baseColorFactor"): return int(offsetof(PushConstants, baseColorFactor));
        case strHash("alphaCutoff"): return int(offsetof(PushConstants, alphaCutoff));
        case strHash("metalRoughFactor"): return int(offsetof(PushConstants, metalRoughFactor));
        case strHash("viewportSize"): return int(offsetof(PushConstants, viewportSize));
        case strHash("emissiveFactor"): return int(offsetof(PushConstants, emissiveFactor));
        case strHash("intensity"): return int(offsetof(PushConstants, intensity));
        case strHash("exposure"): return int(offsetof(PushConstants, exposure));
        default: return -1;
    }
}

void PushConstants::write(u32 nameHash, const void* data, u32 size) {
    const int off = uniformOffset(nameHash);
    if(off < 0) {
        return;
    }
    DEBUG_ASSERT(u32(off) + size <= sizeof(PushConstants));
    std::memcpy(reinterpret_cast<u8*>(this) + off, data, size);
}

void PushConstants::set(u32 nameHash, const UniformValue& value) {
    std::visit([&](const auto& v) {
        write(nameHash, &v, sizeof(v));
    }, value);
}

static bool fileExists(const std::string& path) {
    if(FILE* file = std::fopen(path.c_str(), "rb")) {
        std::fclose(file);
        return true;
    }
    return false;
}

static std::string spirvPath(const std::string& file) {
    const std::string path = std::string(NEBULA_SHADER_PATH) + moduleNameFromFile(file) + ".spv";
    ALWAYS_ASSERT(fileExists(path), ("Unable to find SPIR-V: \"" + path + '"').c_str());
    return path;
}

static std::vector<u32> loadSpirv(const std::string& path) {
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

static VkShaderModule createShaderModule(const std::vector<u32>& spirv) {
    const VkShaderModuleCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size() * sizeof(u32),
        .pCode = spirv.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(vkDevice(), &ci, nullptr, &module));
    return module;
}

// Packs blend, vertex layout, and attachment formats into a u32 used to cache pipeline variants.
static u32 pipelineKey(bool alphaBlend, VertexLayout vertexLayout) {
    return u32(alphaBlend)
         | (u32(vertexLayout) << 1)
         | (u32(ctx().renderingColorFormat) << 4)
         | (u32(ctx().renderingDepthFormat) << 16);
}

static VkPipeline createGraphicsPipeline(VkShaderModule vert, VkShaderModule frag, bool alphaBlend, VertexLayout vertexLayout) {
    const VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "vertexMain",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "fragmentMain",
        },
    };

    // Vertex layout is pipeline state, not rebound per draw.
    // Mesh: Vertex.h locations 0–4. ImGui: ImDrawVert. None: SV_VertexID, no buffer.
    VkVertexInputBindingDescription binding{
        .binding = 0,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[5] = {};
    u32 attrCount = 0;

    if(vertexLayout == VertexLayout::Mesh) {
        binding.stride = u32(sizeof(Vertex));
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, position))};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, normal))};
        attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, u32(offsetof(Vertex, uv))};
        attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, u32(offsetof(Vertex, tangentBitangentSign))};
        attrs[4] = {4, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(Vertex, color))};
        attrCount = 5;
    } else if(vertexLayout == VertexLayout::ImGui) {
        // Packed like ImDrawVert: float2 pos, float2 uv, RGBA8 unorm (20 bytes).
        binding.stride = 20;
        attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
        attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 8};
        attrs[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16};
        attrCount = 3;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    if(attrCount) {
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &binding;
        vertexInputState.vertexAttributeDescriptionCount = attrCount;
        vertexInputState.pVertexAttributeDescriptions = attrs;
    }

    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
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

    VkPipelineColorBlendAttachmentState blendAttachment{
        .blendEnable = alphaBlend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    const VkPipelineDynamicStateCreateInfo dynamic{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 5,
        .pDynamicStates = dynamicStates,
    };

    const VkPipelineRenderingCreateInfo rendering{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &ctx().renderingColorFormat,
        .depthAttachmentFormat = ctx().renderingDepthFormat,
    };

    const VkGraphicsPipelineCreateInfo pipelineCi{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamic,
        .layout = ctx().pipelineLayout,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(vkDevice(), VK_NULL_HANDLE, 1, &pipelineCi, nullptr, &pipeline));
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
    std::swap(_vertModule, other._vertModule);
    std::swap(_fragModule, other._fragModule);
    std::swap(_compModule, other._compModule);
    std::swap(_computePipeline, other._computePipeline);
    std::swap(_pipelines, other._pipelines);
    std::swap(_isCompute, other._isCompute);
}

Program::Program(const std::string& vert, const std::string& frag) {
    loadGraphics(vert, frag);
}

Program::Program(const std::string& comp) : _isCompute(true) {
    loadCompute(comp);
}

void Program::loadGraphics(const std::string& vert, const std::string& frag) {
    _vertModule = createShaderModule(loadSpirv(spirvPath(vert)));
    _fragModule = createShaderModule(loadSpirv(spirvPath(frag)));
}

void Program::loadCompute(const std::string& comp) {
    _compModule = createShaderModule(loadSpirv(spirvPath(comp)));

    const VkComputePipelineCreateInfo pipelineCi{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = _compModule,
            .pName = "computeMain",
        },
        .layout = ctx().pipelineLayout,
    };
    vkCheck(vkCreateComputePipelines(vkDevice(), VK_NULL_HANDLE, 1, &pipelineCi, nullptr, &_computePipeline));
}

void Program::destroy() {
    if(!vkDevice()) {
        return;
    }

    // Enqueue in creation order so reverse flush destroys pipelines before modules.
    deferDestroy(_vertModule);
    deferDestroy(_fragModule);
    deferDestroy(_compModule);
    deferDestroy(_computePipeline);
    for(CachedPipeline& cached : _pipelines) {
        deferDestroy(cached.pipeline);
    }

    _vertModule = VK_NULL_HANDLE;
    _fragModule = VK_NULL_HANDLE;
    _compModule = VK_NULL_HANDLE;
    _computePipeline = VK_NULL_HANDLE;
    _pipelines.clear();
}

Program::~Program() {
    destroy();
}

VkPipeline Program::getOrCreatePipeline(bool alphaBlend, VertexLayout layout) const {
    const u32 key = pipelineKey(alphaBlend, layout);
    for(const CachedPipeline& cached : _pipelines) {
        if(cached.key == key) {
            return cached.pipeline;
        }
    }

    const VkPipeline pipeline = createGraphicsPipeline(
        _vertModule,
        _fragModule,
        alphaBlend,
        layout
    );
    _pipelines.push_back({key, pipeline});
    return pipeline;
}

void Program::bindGraphics(const RasterState& raster, VertexLayout layout, const PushConstants& push) const {
    DEBUG_ASSERT(!_isCompute);
    if(!vkIsRecording()) {
        return;
    }

    const VkCommandBuffer cmd = vkCommandBuffer();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getOrCreatePipeline(raster.alphaBlend, layout));
    vkCmdSetCullMode(cmd, raster.cullMode);
    vkCmdSetDepthTestEnable(cmd, raster.depthTestEnable ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, raster.depthCompareOp);

    if(ctx().pipelineLayout) {
        vkCmdPushConstants(
            cmd,
            ctx().pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(push),
            &push
        );
    }
}

void Program::bindCompute() const {
    DEBUG_ASSERT(_isCompute);
    if(!vkIsRecording()) {
        return;
    }
    vkCmdBindPipeline(vkCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, _computePipeline);
}

bool Program::isCompute() const {
    return _isCompute;
}

std::shared_ptr<Program> Program::fromFile(const std::string& comp) {
    static std::unordered_map<std::string, std::weak_ptr<Program>> loaded;

    auto& weakProgram = loaded[comp];
    auto program = weakProgram.lock();
    if(!program) {
        program = std::make_shared<Program>(comp);
        weakProgram = program;
    }
    return program;
}

std::shared_ptr<Program> Program::fromFiles(const std::string& vert, const std::string& frag) {
    static std::unordered_map<std::string, std::weak_ptr<Program>> loaded;

    auto& weakProgram = loaded[vert + '\n' + frag];
    auto program = weakProgram.lock();
    if(!program) {
        program = std::make_shared<Program>(vert, frag);
        weakProgram = program;
    }
    return program;
}

}
