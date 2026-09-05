import utils;

[[vk::binding(0, 1)]] Sampler2D inEquirec;
[[vk::binding(4, 1)]] RWTexture2DArray<float4> outCube;

// Direction of a cubemap texel: faces 0–5 are ±X, ±Z, ±Y with UV remapped into clip space.
float3 cubeDir(float2 texCoord, uint side) {
    const float2 tex = texCoord * 2.0 - 1.0;

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
void computeMain(uint3 id : SV_DispatchThreadID) {
    uint faceW, faceH, layers;
    outCube.GetDimensions(faceW, faceH, layers);
    const float2 uv = (float2(id.xy) + 0.5) / float2(faceW, faceH);
    const float3 direction = normalize(cubeDir(uv, id.z));
    const float4 color = inEquirec.SampleLevel(toEquirec(direction), 0.0);

    outCube[id] = color;
}
