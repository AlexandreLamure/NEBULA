struct VSOutput {
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float2 uv : UV;
};

// One oversized triangle from SV_VertexID whose UVs cover [0,1]² of the screen.
[shader("vertex")]
VSOutput vertex_main(uint vid : SV_VertexID) {
    float2 uvs[3] = {
        float2(0.0, 2.0),
        float2(0.0, 0.0),
        float2(2.0, 0.0),
    };

    VSOutput output;
    output.uv = uvs[vid];
    output.sv_position = float4((uvs[vid] * 2.0 - 1.0), 0.0, 1.0);
    return output;
}
