#ifndef COMPUTE_PUSH_CONSTANTS_HLSLI
#define COMPUTE_PUSH_CONSTANTS_HLSLI

struct ComputeConstants {
    uint64_t addressThisFrame;
    uint64_t addressLastFrame;
    float deltaTime;
    uint particlesEnabled;
};
[[vk::push_constant]] ConstantBuffer<ComputeConstants> computeConstants;

#endif // COMPUTE_PUSH_CONSTANTS_HLSLI