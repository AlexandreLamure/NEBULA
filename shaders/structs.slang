// GPU-side copies of the C++ structs in shader_structs.h (which still includes structs.glsl).
// Keep the padding: Vulkan UBOs use std140-like rules, so a float3 is followed by 4 bytes.
// Fields are `public` so other modules can read them after `import structs;`.

module structs;

public struct CameraData {
    public float4x4 view_proj;
    public float4x4 inv_view_proj;
    public float3 position;
    public float padding;
};

public struct FrameData {
    public CameraData camera;

    public float3 sun_dir;
    public uint point_light_count;

    public float3 sun_color;
    public float ibl_intensity;
};

public struct PointLight {
    public float3 position;
    public float radius;
    public float3 color;
    public float padding;
};

// Replaces every former GLSL `uniform` (model matrix, material factors, exposure, ...).
// Offsets must match OM3D::PushConstants in Program.h.
public struct PushConstants {
    public float4x4 model;
    public float3 base_color_factor;
    public float alpha_cutoff;
    public float2 metal_rough_factor;
    public float2 viewport_size;
    public float3 emissive_factor;
    public float intensity;
    public float exposure;
};

[[vk::binding(0, 0)]]
public ConstantBuffer<FrameData> frame;

[[vk::push_constant]]
public PushConstants pc;
