#ifndef VERTEX_PUSH_CONSTANTS_HLSLI
#define VERTEX_PUSH_CONSTANTS_HLSLI

// Struct definitions

struct DrawLayout {
    uint transformIndex;
    uint materialIndex;
    bool enabled;           // bool is 4 bytes large!
};

struct LightLayout {
    uint transformIndex;
    float3 color;
    float intensity;
    bool enabled;
};

struct TransformLayout {
    float4 position;
    float4 rotation;
    float4 scale;
};

struct MaterialLayout {
    float4 materialTint;
    uint textureIndex;
    float3 unused;
};

struct BatchLayout {
    uint instanceTransformIndex;
};

// NOTE: Order of variables here has to be the exact same as on CPU side!
struct VertexPushConstants {
    float4 cameraPos;
    float4x4 projection;
    float4x4 view;

    uint64_t drawAddress;
    uint64_t lightAddress;
    uint64_t transformAddress;
    uint64_t materialAddress;

    uint64_t batchAddress;

    uint particlesEnabled;
    int lightMode;
    uint lightCount;

    uint drawIndex;
    uint batchOffset;
};

[[vk::push_constant]]
ConstantBuffer<VertexPushConstants> vertexConstants;

#endif // VERTEX_PUSH_CONSTANTS_HLSLI
