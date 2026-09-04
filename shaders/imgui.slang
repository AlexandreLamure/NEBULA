import structs;
import utils;

[[vk::binding(0, 1)]] Sampler2D in_texture;

struct VSInput {
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float2 uv : UV;
    [[vk::location(2)]] float4 color : COLOR;
};

struct VSOutput {
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float2 uv : UV;
    [[vk::location(1)]] float4 color : COLOR;
};

[shader("vertex")]
VSOutput vertex_main(VSInput input) {
    VSOutput output;
    output.uv = input.uv;
    // R8G8B8A8_UNORM vertex input already maps the packed 8-bit color to 0–1.
    output.color = input.color;

    // Ortho projection: column-major mat4 times float4(pos, 0, 1).
    float4 clip;
    clip.x = (2.0 / pc.viewport_size.x) * input.pos.x - 1.0;
    clip.y = (2.0 / -pc.viewport_size.y) * input.pos.y + 1.0;
    clip.z = 0.0;
    clip.w = 1.0;
    output.sv_position = clip;
    return output;
}

[shader("fragment")]
float4 fragment_main(float2 uv : UV, float4 color : COLOR) : SV_Target {
    const float4 sampled = color * in_texture.Sample(uv);
    // ImGui colors are sRGB. The swapchain is sRGB too, so it converts linear → sRGB
    // at present — undo that by writing linear values here.
    return float4(sRGB_to_linear(sampled.rgb), sampled.a);
}
