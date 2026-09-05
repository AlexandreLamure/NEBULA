// GPU-side copies of the C++ structs in shaderStructs.h.
// Keep the padding: Vulkan UBOs use std140-like rules, so a float3 is followed by 4 bytes.
// Fields are `public` so other modules can read them after `import structs;`.

module structs;

public struct CameraData {
    public float4x4 viewProj;
    public float4x4 invViewProj;
    public float3 position;
    public float padding;
};

public struct FrameData {
    public CameraData camera;

    public float3 sunDir;
    public uint pointLightCount;

    public float3 sunColor;
    public float iblIntensity;
};

public struct PointLight {
    public float3 position;
    public float radius;
    public float3 color;
    public float padding;
};

// Push constants for per-draw data (model matrix, material factors, exposure, ...).
// Offsets must match nebula::PushConstants in Program.h.
public struct PushConstants {
    public float4x4 model;
    public float3 baseColorFactor;
    public float alphaCutoff;
    public float2 metalRoughFactor;
    public float2 viewportSize;
    public float3 emissiveFactor;
    public float intensity;
    public float exposure;
};

[[vk::binding(0, 0)]]
public ConstantBuffer<FrameData> frame;

[[vk::push_constant]]
public PushConstants pc;
