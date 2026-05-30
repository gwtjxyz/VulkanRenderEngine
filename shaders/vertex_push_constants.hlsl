#ifndef VERTEX_PUSH_CONSTANTS_HLSL
#define VERTEX_PUSH_CONSTANTS_HLSL

struct VertexPushConstants {
    uint64_t shaderDataStartAddress;
    uint shaderDataIndex;
    uint particlesEnabled;
};

[[vk::push_constant]] ConstantBuffer<VertexPushConstants> vertexConstants;

#endif // VERTEX_PUSH_CONSTANTS_HLSL