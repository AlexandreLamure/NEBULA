[[vk::binding(0, 1)]] Sampler2D inTex;

[shader("fragment")]
float4 fragmentMain(float2 uv : UV) : SV_Target {
    return inTex.Sample(uv);
}
