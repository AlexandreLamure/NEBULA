#include "Scene.h"
#include "StaticMesh.h"

#include <glm/gtc/quaternion.hpp>

#include <utils.h>

#include <iostream>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#endif

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NOEXCEPTION
#include <tinygltf/tiny_gltf.h>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

namespace nebula {

bool displayGltfLoadingWarnings = false;

static size_t componentCount(int type) {
    switch(type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        case TINYGLTF_TYPE_MAT2: return 4;
        case TINYGLTF_TYPE_MAT3: return 9;
        case TINYGLTF_TYPE_MAT4: return 16;
        default: return 0;
    }
}

// Copies a glTF accessor (stride, offset, optional normalize) into one Vertex field.
static bool decodeAttribBuffer(const tinygltf::Model& gltf, const std::string& name, const tinygltf::Accessor& accessor, Span<Vertex> vertices) {
    const tinygltf::BufferView& buffer = gltf.bufferViews[accessor.bufferView];

    if(accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        if(displayGltfLoadingWarnings) {
            std::cerr << "Unsupported component type (" << accessor.componentType << ") for \"" << name << "\"" << std::endl;
        }
        return false;
    }

    [[maybe_unused]]
    const size_t vertexCount = vertices.size();

    auto decodeAttribs =  [&](auto* vertexElems) {
        using attribType = std::remove_reference_t<decltype(vertexElems[0])>;
        using value_type = typename attribType::value_type;
        static constexpr size_t size = sizeof(attribType) / sizeof(value_type);

        const size_t components = componentCount(accessor.type);
        const bool normalize = accessor.normalized;

        DEBUG_ASSERT(accessor.count == vertexCount);

        if(components != size) {
            if(displayGltfLoadingWarnings) {
                std::cerr << "Expected VEC" << size << " attribute, got VEC" << components << std::endl;
            }
        }

        const size_t minSize = std::min(size, components);
        auto convert = [=](const u8* data) {
            attribType vec;
            for(size_t i = 0; i != minSize; ++i) {
                vec[int(i)] = reinterpret_cast<const value_type*>(data)[i];
            }
            if(normalize) {
                if constexpr(size == 4) {
                    const glm::vec3 n = glm::normalize(glm::vec3(vec));
                    vec[0] = n[0];
                    vec[1] = n[1];
                    vec[2] = n[2];
                } else {
                    vec = glm::normalize(vec);
                }
            }
            return vec;
        };

        {
            u8* outBegin = reinterpret_cast<u8*>(vertexElems);

            const auto& inBuffer = gltf.buffers[buffer.buffer].data;
            const u8* inBegin = inBuffer.data() + buffer.byteOffset + accessor.byteOffset;
            const size_t attribSize = components * sizeof(value_type);
            const size_t inputStride = buffer.byteStride ? buffer.byteStride : attribSize;

            for(size_t i = 0; i != accessor.count; ++i) {
                const u8* attrib = inBegin + i * inputStride;
                DEBUG_ASSERT(attrib < inBuffer.data() + inBuffer.size());
                *reinterpret_cast<attribType*>(outBegin + i * sizeof(Vertex)) = convert(attrib);
            }
        }
    };

    if(name == "POSITION") {
        decodeAttribs(&vertices[0].position);
    } else if(name == "NORMAL") {
        decodeAttribs(&vertices[0].normal);
    } else if(name == "TANGENT") {
        decodeAttribs(&vertices[0].tangentBitangentSign);
    } else if(name == "TEXCOORD_0") {
        decodeAttribs(&vertices[0].uv);
    } else if(name == "COLOR_0") {
        decodeAttribs(&vertices[0].color);
    } else {
        if(displayGltfLoadingWarnings) {
            std::cerr << "Attribute \"" << name << "\" is not supported" << std::endl;
        }
    }
    return true;
}

// Widens glTF 8/16/32-bit indices (with optional byteStride) into a tightly packed u32 buffer.
static bool decodeIndexBuffer(const tinygltf::Model& gltf, const tinygltf::Accessor& accessor, Span<u32> indices) {
    const tinygltf::BufferView& buffer = gltf.bufferViews[accessor.bufferView];

    auto decodeIndices = [&](u32 elemSize, auto convertIndex) {
        const u8* inBuffer = gltf.buffers[buffer.buffer].data.data() + buffer.byteOffset + accessor.byteOffset;
        const size_t inputStride = buffer.byteStride ? buffer.byteStride : elemSize;

        for(size_t i = 0; i != accessor.count; ++i) {
            indices[i] = convertIndex(inBuffer + i * inputStride);
        }
    };

    switch(accessor.componentType) {
        case TINYGLTF_PARAMETER_TYPE_BYTE:
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
            decodeIndices(1, [](const u8* data) -> u32 { return *data; });
        break;

        case TINYGLTF_PARAMETER_TYPE_SHORT:
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
            decodeIndices(2, [](const u8* data) -> u32 { return *reinterpret_cast<const u16*>(data); });
        break;

        case TINYGLTF_PARAMETER_TYPE_INT:
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
            decodeIndices(4, [](const u8* data) -> u32 { return *reinterpret_cast<const u32*>(data); });
        break;

        default:
            std::cerr << "Index component type not supported" << std::endl;
            return false;
    }

    return true;
}

static Result<MeshData> buildMeshData(const tinygltf::Model& gltf, const tinygltf::Primitive& prim) {
    std::vector<Vertex> vertices;
    for(auto&& [name, id] : prim.attributes) {
        tinygltf::Accessor accessor = gltf.accessors[id];
        if(!accessor.count) {
            continue;
        }

        if(accessor.sparse.isSparse) {
            return {false, {}};
        }

        if(!vertices.size()) {
            std::fill_n(std::back_inserter(vertices), accessor.count, Vertex{});
        } else if(vertices.size() != accessor.count) {
            return {false, {}};
        }

        if(!decodeAttribBuffer(gltf, name, accessor, vertices)) {
            return {false, {}};
        }
    }

    std::vector<u32> indices;
    {
        tinygltf::Accessor accessor = gltf.accessors[prim.indices];
        if(!accessor.count || accessor.sparse.isSparse) {
            return {false, {}};
        }

        if(!indices.size()) {
            std::fill_n(std::back_inserter(indices), accessor.count, u32(0));
        } else if(indices.size() != accessor.count) {
            return {false, {}};
        }

        if(!decodeIndexBuffer(gltf, accessor, indices)) {
            return {false, {}};
        }
    }

    return {true, MeshData{std::move(vertices), std::move(indices)}};
}

static Result<TextureData> buildTextureData(const tinygltf::Image& image, bool asSRGB) {
    if(image.bits != 8 && image.pixel_type != TINYGLTF_COMPONENT_TYPE_BYTE && image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        std::cerr << "Unsupported image format (pixel type)" << std::endl;
        return {false, {}};
    }

    ImageFormat format = ImageFormat::RGBA8_UNORM;
    switch(image.component) {
        case 3:
            format = asSRGB ? ImageFormat::RGB8_sRGB : ImageFormat::RGB8_UNORM;
        break;

        case 4:
            format = asSRGB ? ImageFormat::RGBA8_sRGB : ImageFormat::RGBA8_UNORM;
        break;

        default:
            std::cerr << "Unsupported image format (components)" << std::endl;
            return {false, {}};
    }

    auto data = std::make_unique<u8[]>(image.image.size());
    std::copy(image.image.begin(), image.image.end(), data.get());

    return {true, TextureData{std::move(data), glm::uvec2(image.width, image.height), format}};
}


// Builds T * R * S from glTF TRS; rotation is stored xyzw and converted to glm's wxyz quaternion.
static glm::mat4 parseNodeMatrix(const tinygltf::Node& node) {
    glm::vec3 translation(0.0f, 0.0f, 0.0f);
    for(u32 k = 0; k != node.translation.size(); ++k) {
        translation[k] = float(node.translation[k]);
    }

    glm::vec3 scale(1.0f, 1.0f, 1.0f);
    for(u32 k = 0; k != node.scale.size(); ++k) {
        scale[k] = float(node.scale[k]);
    }

    glm::vec4 rotation(0.0f, 0.0f, 0.0f, 1.0f);
    for(u32 k = 0; k != node.rotation.size(); ++k) {
        rotation[k] = float(node.rotation[k]);
    }

    const glm::tquat<float> q(rotation.w, rotation.x, rotation.y, rotation.z);
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), scale);
}

static glm::mat4 baseTransform() {
    return glm::mat4(1.0f);
}

static void parseNodeTransforms(int nodeIndex, const tinygltf::Model& gltf, std::unordered_map<int, glm::mat4>& nodeTransforms, const glm::mat4& parentTransform = baseTransform()) {
    const tinygltf::Node& node = gltf.nodes[nodeIndex];
    const glm::mat4 transform = parentTransform * parseNodeMatrix(node);
    nodeTransforms[nodeIndex] = transform;
    for(int child : node.children)  {
        parseNodeTransforms(child, gltf, nodeTransforms, transform);
    }
}

// Accumulates a per-vertex tangent from each triangle's position/UV deltas (Lengyel), then normalizes and sets bitangent sign to +1.
static void computeTangents(MeshData& mesh) {
    for(Vertex& vert : mesh.vertices) {
        vert.tangentBitangentSign = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    for(size_t i = 0; i < mesh.indices.size(); i += 3) {
        const u32 tri[] = {
            mesh.indices[i + 0],
            mesh.indices[i + 1],
            mesh.indices[i + 2]
        };

        const glm::vec3 edges[] = {
            mesh.vertices[tri[1]].position - mesh.vertices[tri[0]].position,
            mesh.vertices[tri[2]].position - mesh.vertices[tri[0]].position
        };

        const glm::vec2 uvs[] = {
            mesh.vertices[tri[0]].uv,
            mesh.vertices[tri[1]].uv,
            mesh.vertices[tri[2]].uv
        };

        const float dt[] = {
            uvs[1].y - uvs[0].y,
            uvs[2].y - uvs[0].y
        };

        const glm::vec3 tangent = -glm::normalize((edges[0] * dt[1]) - (edges[1] * dt[0]));
        mesh.vertices[tri[0]].tangentBitangentSign += glm::vec4(tangent, 0.0f);
        mesh.vertices[tri[1]].tangentBitangentSign += glm::vec4(tangent, 0.0f);
        mesh.vertices[tri[2]].tangentBitangentSign += glm::vec4(tangent, 0.0f);
    }

    for(Vertex& vert : mesh.vertices) {
        const glm::vec3 tangent = vert.tangentBitangentSign;
        vert.tangentBitangentSign = glm::vec4(glm::normalize(tangent), 1.0f);
    }
}


Result<std::unique_ptr<Scene>> Scene::fromGltf(const std::string& fileName) {
    const double time = programTime();
    DEFER(std::cout << fileName << " loaded in " << std::round((programTime() - time) * 100.0) / 100.0 << "s" << std::endl);

    tinygltf::TinyGLTF gltfCtx;
    tinygltf::Model gltf;

    {
        std::string err;
        std::string warn;

        const bool isAscii = endsWith(fileName, ".gltf");
        const bool ok = isAscii
                ? gltfCtx.LoadASCIIFromFile(&gltf, &err, &warn, fileName)
                : gltfCtx.LoadBinaryFromFile(&gltf, &err, &warn, fileName);

        if(!err.empty()) {
            std::cerr << "Error while loading gltf: " << err << std::endl;
        }
        if(!warn.empty()) {
            std::cerr << "Warning while loading gltf: " << warn << std::endl;
        }

        if(!ok) {
            return {false, {}};
        }
    }

    std::cout << fileName << " parsed in " << std::round((programTime() - time) * 100.0) / 100.0 << "s" << std::endl;

    auto scene = std::make_unique<Scene>();

    std::unordered_map<int, std::shared_ptr<Texture>> textures;
    std::unordered_map<int, std::shared_ptr<Material>> materials;
    std::unordered_map<int, glm::mat4> nodeTransforms;
    std::vector<std::pair<int, int>> lightNodes;

    {
        std::vector<int> nodeIndices;
        if(gltf.defaultScene >= 0) {
            nodeIndices = gltf.scenes[gltf.defaultScene].nodes;
        } else {
            for(u32 i = 0; i != gltf.nodes.size(); ++i) {
                nodeIndices.push_back(i);
                nodeTransforms[i] = baseTransform();
            }
        }

        for(const int nodeIndex : nodeIndices) {
            parseNodeTransforms(nodeIndex, gltf, nodeTransforms);
        }

        for(const int nodeIndex : nodeIndices) {
            const auto& node = gltf.nodes[nodeIndex];
            if(const auto it = node.extensions.find("KHR_lights_punctual"); it != node.extensions.end()) {
                const int lightIndex = it->second.Get("light").Get<int>();
                if(lightIndex < 0 || lightIndex >= static_cast<int>(gltf.lights.size())) {
                    continue;
                }
                lightNodes.emplace_back(std::pair{nodeIndex, lightIndex});
            }
        }
    }

    const std::string emissiveStrengthExtName = "KHR_materials_emissive_strength";

    for(auto [nodeIndex, nodeTransform] : nodeTransforms) {
        const tinygltf::Node& node = gltf.nodes[nodeIndex];

        if(node.mesh < 0) {
            continue;
        }

        const tinygltf::Mesh& mesh = gltf.meshes[node.mesh];

        const std::shared_ptr<Material> defaultMaterial = std::make_shared<Material>(Material::texturedPbrMaterial());
        for(size_t j = 0; j != mesh.primitives.size(); ++j) {
            const tinygltf::Primitive& prim = mesh.primitives[j];

            if(prim.mode != TINYGLTF_MODE_TRIANGLES) {
                continue;
            }

            auto mesh = buildMeshData(gltf, prim);
            if(!mesh.isOk) {
                return {false, {}};
            }

            if(mesh.value.vertices[0].tangentBitangentSign == glm::vec4(0.0f)) {
                computeTangents(mesh.value);
            }

            std::shared_ptr<Material> material = defaultMaterial;
            if(prim.material >= 0) {
                auto& mat = materials[prim.material];

                if(!mat) {
                    const auto& gltfMat = gltf.materials[prim.material];
                    const auto& albedoInfo = gltfMat.pbrMetallicRoughness.baseColorTexture;
                    const auto& normalInfo = gltfMat.normalTexture;
                    const auto& metalRoughInfo = gltfMat.pbrMetallicRoughness.metallicRoughnessTexture;
                    const auto& emissiveInfo = gltfMat.emissiveTexture;

                    auto loadTexture = [&](auto textureInfo, bool asSRGB) -> std::shared_ptr<Texture> {
                        if(textureInfo.texCoord != 0) {
                            std::cerr << "Unsupported texture coordinate channel (" << textureInfo.texCoord << ")" << std::endl;
                            return nullptr;
                        }

                        if(textureInfo.index < 0) {
                            return nullptr;
                        }

                        const int index = gltf.textures[textureInfo.index].source;
                        if(index < 0) {
                            return nullptr;
                        }

                        auto& texture = textures[index];
                        if(!texture) {
                            if(const auto r = buildTextureData(gltf.images[index], asSRGB); r.isOk) {
                                texture = std::make_shared<Texture>(r.value);
                            }
                        }
                        return texture;
                    };

                    const bool opaque = (gltfMat.alphaMode == "OPAQUE") || (gltfMat.alphaMode == "NONE");
                    const bool mask = (gltfMat.alphaMode == "MASK");
                    const bool alphaTest = !opaque || mask;

                    auto albedo = loadTexture(albedoInfo, true);
                    auto normal = loadTexture(normalInfo, false);
                    auto metalRough = loadTexture(metalRoughInfo, false);
                    auto emissive = loadTexture(emissiveInfo, false);


                    mat = std::make_shared<Material>(Material::texturedPbrMaterial(alphaTest));

                    if(!opaque && !mask) {
                        mat->setBlendMode(BlendMode::Alpha);
                        mat->setDepthTestMode(DepthTestMode::None);
                    }

                    if(albedo) {
                        mat->setTexture(0u, albedo);
                    }

                    if(normal) {
                        mat->setTexture(1u, normal);
                    }

                    if(metalRough) {
                        mat->setTexture(2u, metalRough);
                    }

                    if(emissive) {
                        mat->setTexture(3u, emissive);
                    }


                    if(alphaTest) {
                        mat->setStoredUniform(HASH("alphaCutoff"), float(gltfMat.alphaCutoff));
                    }

                    mat->setDoubleSided(gltfMat.doubleSided);

                    mat->setStoredUniform(HASH("baseColorFactor"), glm::vec3(
                        gltfMat.pbrMetallicRoughness.baseColorFactor[0],
                        gltfMat.pbrMetallicRoughness.baseColorFactor[1],
                        gltfMat.pbrMetallicRoughness.baseColorFactor[2]
                    ));

                    mat->setStoredUniform(HASH("metalRoughFactor"), glm::vec2(
                        gltfMat.pbrMetallicRoughness.metallicFactor,
                        gltfMat.pbrMetallicRoughness.roughnessFactor
                    ));



                    float emissiveFactor = 1.0f;
                    if(const auto it = gltfMat.extensions.find(emissiveStrengthExtName); it != gltfMat.extensions.end()) {
                        emissiveFactor = float(it->second.Get("emissiveStrength").GetNumberAsDouble());
                    }

                    mat->setStoredUniform(HASH("emissiveFactor"), glm::vec3(
                        gltfMat.emissiveFactor[0],
                        gltfMat.emissiveFactor[1],
                        gltfMat.emissiveFactor[2]
                    ) * emissiveFactor);
                }

                material = mat;
            }

            auto sceneObject = SceneObject(std::make_shared<StaticMesh>(mesh.value), std::move(material));
            sceneObject.setTransform(nodeTransform);
            scene->addObject(std::move(sceneObject));
        }
    }

    for(auto [nodeIndex, lightIndex] : lightNodes) {
        const auto& gltfLight = gltf.lights[lightIndex];

        const glm::vec3 color = glm::vec3(float(gltfLight.color[0]), float(gltfLight.color[1]), float(gltfLight.color[2])) * float(gltfLight.intensity);;

        PointLight light;
        light.setPosition(nodeTransforms[nodeIndex][3]);
        light.setColor(color);
        if(gltfLight.range > 0.0) {
            light.setRadius(float(gltfLight.range));
        } else {
            const float intensity = glm::dot(color, glm::vec3(1.0f));
            light.setRadius(std::sqrt(intensity * 100.0f)); // Put radius where lum < 1%
        }
        scene->addLight(light);
    }


    return {true, std::move(scene)};
}

}

