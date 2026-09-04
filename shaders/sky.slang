import structs;
import utils;

[[vk::binding(2, 0)]] SamplerCube in_envmap;

// Unprojects the pixel at a far reverse-Z depth to a view ray, then samples the env cubemap along it.
[shader("fragment")]
float4 fragment_main(float2 uv : UV) : SV_Target {
    const float3 view_dir = normalize(unproject(uv, 0.001, frame.camera.inv_view_proj) - frame.camera.position);
    return in_envmap.Sample(view_dir) * pc.intensity;
}
