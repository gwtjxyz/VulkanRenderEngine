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
    uint enabled;
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

/** vk::BufferPointer class:
    (from https://github.com/microsoft/hlsl-specs/blob/main/proposals/0010-vk-buffer-ref.md)

    template <struct S, int align>
    class vk::BufferPointer {
        vk::BufferPointer(const vk::BufferPointer&);
        vk::BufferPointer& operator=(const vk::BufferPointer&);
        vk::BufferPointer(const uint64_t);
        S& Get() const;
        operator uint64_t() const;
    }

    can do this later maybe?
*/

// NOTE: Order of variables here has to be the exact same as on CPU side!
struct VertexPushConstants {
    float4x4 projection;
    float4x4 view;

    uint64_t drawAddress;
    uint64_t lightAddress;
    uint64_t transformAddress;
    uint64_t materialAddress;

    uint particlesEnabled;
    int lightMode;
    uint lightCount;

    uint drawIndex;
};

[[vk::push_constant]]
ConstantBuffer<VertexPushConstants> vertexConstants;

#endif // VERTEX_PUSH_CONSTANTS_HLSLI
