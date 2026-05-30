#ifndef COMPUTE_PUSH_CONSTANTS_HLSL
#define COMPUTE_PUSH_CONSTANTS_HLSL

struct ComputeConstants {
    uint64_t addressThisFrame;
    uint64_t addressLastFrame;
    float deltaTime;
    uint particlesEnabled;
};
[[vk::push_constant]] ConstantBuffer<ComputeConstants> computeConstants;

#endif // COMPUTE_PUSH_CONSTANTS_HLSL