[[vk::binding(0, 1)]] Sampler2D in_tex;

[shader("fragment")]
float4 fragment_main(float2 uv : UV) : SV_Target {
    return in_tex.Sample(uv);
}
