import structs;
import utils;
import lighting;

// Vulkan descriptor slots. Texture slots 0–3 → bindings 2–5;
// envmap / BRDF LUT (slots 4–5) → bindings 6–7.
[[vk::binding(1, 0)]] StructuredBuffer<PointLight> point_lights;
[[vk::binding(2, 0)]] Sampler2D in_texture;
[[vk::binding(3, 0)]] Sampler2D in_normal_texture;
[[vk::binding(4, 0)]] Sampler2D in_metal_rough;
[[vk::binding(5, 0)]] Sampler2D in_emissive;
[[vk::binding(6, 0)]] SamplerCube in_envmap;
[[vk::binding(7, 0)]] Sampler2D brdf_lut;

struct FSInput {
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : UV;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float3 position : WORLD_POS;
    [[vk::location(4)]] float3 tangent : TANGENT;
    [[vk::location(5)]] float3 bitangent : BITANGENT;
};

[shader("fragment")]
float4 fragment_main(FSInput input) : SV_Target {
    const float3 normal_map = unpack_normal_map(in_normal_texture.Sample(input.uv).xy);
    const float3 normal = normal_map.x * input.tangent +
                          normal_map.y * input.bitangent +
                          normal_map.z * input.normal;

    const float4 albedo_tex = in_texture.Sample(input.uv);
    const float3 base_color = input.color.rgb * albedo_tex.rgb * pc.base_color_factor;
    const float alpha = albedo_tex.a;

#ifdef ALPHA_TEST
    if(alpha <= pc.alpha_cutoff) {
        discard;
    }
#endif

    const float4 metal_rough_tex = in_metal_rough.Sample(input.uv);
    const float roughness = metal_rough_tex.g * pc.metal_rough_factor.y;
    const float metallic = metal_rough_tex.b * pc.metal_rough_factor.x;

    const float3 to_view = (frame.camera.position - input.position);
    const float3 view_dir = normalize(to_view);

    float3 acc = in_emissive.Sample(input.uv).rgb * pc.emissive_factor;
    acc += eval_ibl(in_envmap, brdf_lut, normal, view_dir, base_color, metallic, roughness) * frame.ibl_intensity;
    {
        acc += frame.sun_color * eval_brdf(normal, view_dir, frame.sun_dir, base_color, metallic, roughness);

        for(uint i = 0; i != frame.point_light_count; ++i) {
            PointLight light = point_lights[i];
            const float3 to_light = (light.position - input.position);
            const float dist = length(to_light);
            const float3 light_vec = to_light / dist;

            const float att = attenuation(dist, light.radius);
            if(att <= 0.0) {
                continue;
            }

            acc += eval_brdf(normal, view_dir, light_vec, base_color, metallic, roughness) * att * light.color;
        }
    }

    float4 out_color = float4(acc, alpha);

#ifdef DEBUG_NORMAL
    out_color = float4(normal * 0.5 + 0.5, 1.0);
#endif

#ifdef DEBUG_METAL
    out_color = float4(metallic, metallic, metallic, 1.0);
#endif

#ifdef DEBUG_ROUGH
    out_color = float4(roughness, roughness, roughness, 1.0);
#endif

#ifdef DEBUG_ENV
    out_color = float4(in_envmap.Sample(normal).rgb, 1.0);
#endif

    return out_color;
}
