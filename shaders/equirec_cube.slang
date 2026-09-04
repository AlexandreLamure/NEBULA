import utils;

[[vk::binding(0, 1)]] Sampler2D in_equirec;
[[vk::binding(4, 1)]] RWTexture2DArray<float4> out_cube;

// Direction of a cubemap texel: faces 0–5 are ±X, ±Z, ±Y with UV remapped into clip space.
float3 cube_dir(float2 tex_coord, uint side) {
    const float2 tex = tex_coord * 2.0 - 1.0;

    switch(side) {
        case 0: return float3(1.0, -tex.x, -tex.y);
        case 1: return float3(-1.0, tex.x, -tex.y);
        case 2: return float3(tex.x, tex.y, 1.0);
        case 3: return float3(tex.x, -tex.y, -1.0);
        case 4: return float3(tex.x, 1.0, -tex.y);
        case 5: return float3(-tex.x, -1.0, -tex.y);
    }

    return float3(0.0, 0.0, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 id : SV_DispatchThreadID) {
    uint face_w, face_h, layers;
    out_cube.GetDimensions(face_w, face_h, layers);
    const float2 uv = (float2(id.xy) + 0.5) / float2(face_w, face_h);
    const float3 direction = normalize(cube_dir(uv, id.z));
    const float4 color = in_equirec.SampleLevel(to_equirec(direction), 0.0);

    out_cube[id] = color;
}
