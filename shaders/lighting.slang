module lighting;

import utils;

// Trowbridge-Reitz (GGX) normal distribution: how much of the microfacet lobe aligns with H.
public float d_ggx(float3 N, float3 H, float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float NdotH = max(dot(N, H), 0.0);
    const float NdotH2 = NdotH * NdotH;

    const float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Schlick G1 given a precomputed remapping k (see g_smith for how k is chosen).
public float g_schlick_ggx(float NdotV, float k) {
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Height-correlated Smith G with Disney's remapped k = (r+1)²/8 for analytic (punctual) lights.
public float g_smith(float3 N, float3 V, float3 L, float roughness) {
    const float r = (roughness + 1.0);
    const float k = (r * r) / 8.0;
    const float ggx2 = g_schlick_ggx(max(dot(N, V), 0.0), k);
    const float ggx1 = g_schlick_ggx(max(dot(N, L), 0.0), k);
    return ggx1 * ggx2;
}

// Schlick Fresnel: reflectance rises from F0 to 1 as the view grazes the surface.
public float3 f_schlick(float cos_theta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// Schlick Fresnel whose grazing color is dimmed by roughness so IBL spec doesn't over-brighten.
public float3 f_schlick_roughness(float cos_theta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// Cook-Torrance metalness BRDF: D*G*F specular plus Lambert diffuse, then scaled by NdotL.
public float3 eval_brdf(float3 N, float3 V, float3 L, float3 albedo, float metallic, float roughness) {
    const float NdotL = dot(N, L);

    if(NdotL <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 H = normalize(V + L);

    const float D = d_ggx(N, H, roughness);
    const float G = g_smith(N, V, L, roughness);

    const float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 F = f_schlick(max(dot(H, V), 0.0), F0);

    const float specular = (D * G) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    const float3 diffuse = albedo * INV_PI * (1.0 - metallic);

    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;

    return (diffuse * kD + specular * kS) * NdotL;
}

// Maps roughness to a prefiltered-env mip with a 0.25 gamma so glossy values use more levels.
public float roughness_to_mip(float r, uint mips) {
    return pow(r, 0.25) * (mips - 1);
}

// Split-sum IBL: irradiance from the last mip, prefiltered spec from roughness, scaled by the BRDF LUT.
public float3 eval_ibl(SamplerCube envmap, Sampler2D brdf_lut, float3 N, float3 V, float3 albedo, float metallic, float roughness) {
    const float NdotV = max(dot(N, V), 0.0);

    const float3 irradiance = envmap.SampleLevel(N, 9999).rgb;
    const float3 diffuse = irradiance * albedo;

    const float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 F = f_schlick_roughness(NdotV, F0, roughness);

    uint env_w, env_h, env_mips;
    envmap.GetDimensions(0, env_w, env_h, env_mips);
    const uint texture_max_level = env_mips - 1;
    const float3 prefiltered = envmap.SampleLevel(reflect(-V, N), roughness_to_mip(roughness, texture_max_level)).rgb;

    const float2 brdf = brdf_lut.Sample(float2(NdotV, roughness)).rg;
    const float3 specular = prefiltered * (F * brdf.x + brdf.y);

    const float3 kS = F;
    const float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);

    return kD * diffuse + specular;
}
