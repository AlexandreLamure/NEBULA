import utils;

[[vk::binding(8, 0)]]
RWTexture2D<float2> out_image;

float2 integrate_brdf(float NdotV, float roughness) {
    float A = 0.0;
    float B = 0.0;

    const float3 N = float3(0.0, 0.0, 1.0);
    const float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    const uint SAMPLE_COUNT = 1024u;
    for(uint i = 0; i != SAMPLE_COUNT; ++i) {
        const float2 Xi = hammersley(i, SAMPLE_COUNT);
        const float3 H = importance_sample_ggx(Xi, N, roughness);
        const float3 L = normalize(2.0 * dot(V, H) * H - V);

        if(L.z > 0.0) {
            const float VdotH = max(dot(V, H), 0.0);
            const float G = geometry_smith(N, V, L, roughness);
            const float G_Vis = (G * VdotH) / (max(H.z, 0.0) * NdotV);
            const float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    return float2(A, B) / float(SAMPLE_COUNT);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 id : SV_DispatchThreadID) {
    uint2 size;
    out_image.GetDimensions(size.x, size.y);
    const float2 uv = (float2(id.xy) + 0.5) / float2(size);

    out_image[id.xy] = integrate_brdf(uv.x, uv.y);
}
