import structs;
import utils;

[[vk::binding(2, 0)]] SamplerCube inEnvmap;

// Unprojects the pixel at a far reverse-Z depth to a view ray, then samples the env cubemap along it.
[shader("fragment")]
float4 fragmentMain(float2 uv : UV) : SV_Target {
    const float3 viewDir = normalize(unproject(uv, 0.001, frame.camera.invViewProj) - frame.camera.position);
    return inEnvmap.Sample(viewDir) * pc.intensity;
}
