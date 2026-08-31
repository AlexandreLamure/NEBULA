import structs;

struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : UV;
    [[vk::location(3)]] float4 tangent_bitangent_sign : TANGENT;
    [[vk::location(4)]] float3 color : COLOR;
};

struct VSOutput {
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL;
    [[vk::location(1)]] float2 uv : UV;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float3 position : WORLD_POS;
    [[vk::location(4)]] float3 tangent : TANGENT;
    [[vk::location(5)]] float3 bitangent : BITANGENT;
};

// Reconstructs world TBN; bitangent handedness is stored in tangent.w (glTF convention).
[shader("vertex")]
VSOutput vertex_main(VSInput input) {
    VSOutput output;

    const float4 position = mul(pc.model, float4(input.pos, 1.0));
    const float3x3 model3 = float3x3(pc.model);

    output.normal = normalize(mul(model3, input.normal));
    output.tangent = normalize(mul(model3, input.tangent_bitangent_sign.xyz));
    output.bitangent = cross(output.tangent, output.normal) * (input.tangent_bitangent_sign.w > 0.0 ? 1.0 : -1.0);

    output.uv = input.uv;
    output.color = input.color;
    output.position = position.xyz;

    output.sv_position = mul(frame.camera.view_proj, position);
    return output;
}
