#include "compute_push_constants.hlsl"

struct Particle {
    float4 position;
    float4 color;
    float2 velocity;
};

[numthreads(256, 1, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID) {
    uint index = threadId.x;
    uint64_t particleInPtr = computeConstants.addressLastFrame + sizeof(Particle) * index;
    uint64_t particleOutPtr = computeConstants.addressThisFrame + sizeof(Particle) * index;

    Particle particleIn = vk::RawBufferLoad<Particle>(particleInPtr, 4);
    Particle particleOut;

    // The deltaTime that we're getting is in seconds, we convert into ms and double it
    float speed = computeConstants.deltaTime * 2000.0;
    // Limit speed in case of a big freeze, so particles don't fly way out of bounds
    if (speed >= 100.0) speed = 100.0;

    particleOut.position = particleIn.position + float4(particleIn.velocity.xy * speed, 0.0, 0.0);
    particleOut.velocity = particleIn.velocity;

    // Flip movement at window border
    if ((particleOut.position.x <= -1.0) || (particleOut.position.x >= 1.0)) {
        particleOut.velocity.x = -particleOut.velocity.x;
    }
    if ((particleOut.position.y <= -1.0) || (particleOut.position.y >= 1.0)) {
        particleOut.velocity.y = -particleOut.velocity.y;
    }

    // Store computed particle
    vk::RawBufferStore<Particle>(particleOutPtr, particleOut, 4);
}
