module utils;

public static const float PI = 3.14159265359;
public static const float INV_PI = 0.31830988618;

public float sqr(float x) {
    return x * x;
}

public float saturate(float x) {
    return min(1.0, max(0.0, x));
}

public float2 saturate(float2 x) {
    return min(float2(1.0, 1.0), max(float2(0.0, 0.0), x));
}

public float3 saturate(float3 x) {
    return min(float3(1.0, 1.0, 1.0), max(float3(0.0, 0.0, 0.0), x));
}

public float4 saturate(float4 x) {
    return min(float4(1.0, 1.0, 1.0, 1.0), max(float4(0.0, 0.0, 0.0, 0.0), x));
}

// Rec.709 luma weights (linear RGB → perceived brightness).
public float luminance(float3 rgb) {
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

// Unreal-style windowed inverse-square: (1-(d/r)^4)^2 / (d^2+1), zero past radius.
public float attenuation(float distance, float radius) {
    const float x = min(distance, radius);
    return sqr(1.0 - sqr(sqr(x / radius))) / (sqr(x) + 1.0);
}

public float attenuation(float distance, float radius, float falloff) {
    return attenuation(distance * falloff, radius * falloff);
}

public float sRGB_to_linear(float x) {
    if(x <= 0.04045) {
        return x / 12.92;
    }
    return pow((x + 0.055) / 1.055, 2.4);
}

public float linear_to_sRGB(float x) {
    if(x <= 0.0031308) {
        return x * 12.92;
    }
    return 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

public float3 sRGB_to_linear(float3 v) {
    return float3(sRGB_to_linear(v.r), sRGB_to_linear(v.g), sRGB_to_linear(v.b));
}

public float3 linear_to_sRGB(float3 v) {
    return float3(linear_to_sRGB(v.r), linear_to_sRGB(v.g), linear_to_sRGB(v.b));
}

// Maps two-channel [0,1] XY to tangent-space [-1,1] and reconstructs Z as 1 - length(xy).
public float3 unpack_normal_map(float2 normal) {
    normal = normal * 2.0 - float2(1.0, 1.0);
    return float3(normal, 1.0 - sqrt(dot(normal, normal)));
}

// Maps a unit direction to lat-long UV (atan2/asin scaled by 1/2π and 1/π).
public float2 to_equirec(float3 v) {
    return -float2(atan2(-v.y, v.x), asin(v.z)) * float2(0.1591, 0.3183) + 0.5;
}

// Homogeneous divide after multiplying NDC by the inverse view-projection.
public float3 unproject_ndc(float3 ndc, float4x4 inv_matrix) {
    const float4 p = mul(inv_matrix, float4(ndc, 1.0));
    return p.xyz / p.w;
}

// Builds NDC from UV and depth then unprojects to world (depth 1 is near under reverse-Z).
public float3 unproject(float2 uv, float depth, float4x4 inv_matrix) {
    const float3 ndc = float3(uv * 2.0 - 1.0, depth);
    return unproject_ndc(ndc, inv_matrix);
}

// 2D low-discrepancy sample: i/N plus the van der Corput radical inverse (bit-reversed i / 2^32).
public float2 hammersley(uint i, uint N) {
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float2(float(i) / float(N), float(bits) * 2.3283064365386963e-10);
}

// Samples a GGX half-vector from Xi, then rotates it from tangent space into the world around N.
public float3 importance_sample_ggx(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;

    const float phi = 2.0 * PI * Xi.x;
    const float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    const float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
    const float3 H = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    const float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, N));
    const float3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// Smith-Schlick G1 with IBL k = a²/2 (Karis), used when integrating the BRDF LUT.
public float geometry_schlick_ggx(float NdotV, float roughness) {
    const float k = (roughness * roughness) / 2.0;
    const float denom = NdotV * (1.0 - k) + k;

    return NdotV / denom;
}

public float geometry_smith(float3 N, float3 V, float3 L, float roughness) {
    const float ggx2 = geometry_schlick_ggx(max(dot(N, V), 0.0), roughness);
    const float ggx1 = geometry_schlick_ggx(max(dot(N, L), 0.0), roughness);

    return ggx1 * ggx2;
}
