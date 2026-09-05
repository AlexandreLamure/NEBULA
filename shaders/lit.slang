import structs;
import utils;
import lighting;

// Set 0 = frame (persistent). Set 1 = pass (material textures).
[[vk::binding(1, 0)]] StructuredBuffer<PointLight> pointLights;
[[vk::binding(2, 0)]] SamplerCube inEnvmap;
[[vk::binding(3, 0)]] Sampler2D brdfLut;
[[vk::binding(0, 1)]] Sampler2D inTexture;
[[vk::binding(1, 1)]] Sampler2D inNormalTexture;
[[vk::binding(2, 1)]] Sampler2D inMetalRough;
[[vk::binding(3, 1)]] Sampler2D inEmissive;

struct FSInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : UV;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float3 position : WORLD_POS;
    [[vk::location(4)]] float3 tangent : TANGENT;
    [[vk::location(5)]] float3 bitangent : BITANGENT;
};

[shader("fragment")]
float4 fragmentMain(FSInput input) : SV_Target {
    const float3 normalMap = unpackNormalMap(inNormalTexture.Sample(input.uv).xy);
    const float3 normal = normalMap.x * input.tangent +
                          normalMap.y * input.bitangent +
                          normalMap.z * input.normal;

    const float4 albedoTex = inTexture.Sample(input.uv);
    const float3 baseColor = input.color.rgb * albedoTex.rgb * pc.baseColorFactor;
    const float alpha = albedoTex.a;

#ifdef ALPHA_TEST
    if(alpha <= pc.alphaCutoff) {
        discard;
    }
#endif

    const float4 metalRoughTex = inMetalRough.Sample(input.uv);
    const float roughness = metalRoughTex.g * pc.metalRoughFactor.y;
    const float metallic = metalRoughTex.b * pc.metalRoughFactor.x;

    const float3 toView = (frame.camera.position - input.position);
    const float3 viewDir = normalize(toView);

    float3 acc = inEmissive.Sample(input.uv).rgb * pc.emissiveFactor;
    acc += evalIbl(inEnvmap, brdfLut, normal, viewDir, baseColor, metallic, roughness) * frame.iblIntensity;
    {
        acc += frame.sunColor * evalBrdf(normal, viewDir, frame.sunDir, baseColor, metallic, roughness);

        for(uint i = 0; i != frame.pointLightCount; ++i) {
            PointLight light = pointLights[i];
            const float3 toLight = (light.position - input.position);
            const float dist = length(toLight);
            const float3 lightVec = toLight / dist;

            const float att = attenuation(dist, light.radius);
            if(att <= 0.0) {
                continue;
            }

            acc += evalBrdf(normal, viewDir, lightVec, baseColor, metallic, roughness) * att * light.color;
        }
    }

    float4 outColor = float4(acc, alpha);

#ifdef DEBUG_NORMAL
    outColor = float4(normal * 0.5 + 0.5, 1.0);
#endif

#ifdef DEBUG_METAL
    outColor = float4(metallic, metallic, metallic, 1.0);
#endif

#ifdef DEBUG_ROUGH
    outColor = float4(roughness, roughness, roughness, 1.0);
#endif

#ifdef DEBUG_ENV
    outColor = float4(inEnvmap.Sample(normal).rgb, 1.0);
#endif

    return outColor;
}
