import structs;
import utils;

[[vk::binding(0, 1)]] Sampler2D in_hdr;

// Narkowicz's fitted ACES filmic curve, compressing HDR into [0,1].
float3 aces(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float reinhard(float hdr) {
    return hdr / (hdr + 1.0);
}

float3 reinhard(float3 x) {
    return float3(reinhard(x.x), reinhard(x.y), reinhard(x.z));
}

[shader("fragment")]
float4 fragment_main(float2 uv : UV) : SV_Target {
    const float3 hdr = in_hdr.SampleLevel(uv, 0).rgb * pc.exposure;
    float3 tone_mapped = aces(hdr);

    return float4(tone_mapped, 1.0);
}
