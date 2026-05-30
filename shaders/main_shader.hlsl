#include "vertex_push_constants.hlsl"

struct VSInput {
    float4 inPosition: POSITION0;
    float4 inNormal: NORMAL0;
    float2 inUV: TEXCOORD0;
};

// Not sure why this works without these tags, and why using them causes compiler errors
// [[vk::combinedImageSampler]]
Texture2D textureArray[] : register(t0);
// [[vk::combinedImageSampler]]
SamplerState samplerArray[] : register(s0);

struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model;
    float4 lightPos;
    uint lightingEnabled;
    uint textureIndex;
    uint64_t padding;
};

// Obviously very inefficient (storing flags as uints instead of bits), but for just toying around it's not a big deal
struct VSOutput {
    float4 pos : SV_POSITION;
    float4 normal : NORMAL0;
    float2 UV: TEXCOORD0;
    float3 lightVec: VECTOR0;
    float3 viewVec: VECTOR1;
    uint lightingEnabled : TEXCOORD1;
    uint textureIndex : TEXCOORD2;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    // DXC's way of supporting buffer_device_address extension, since DX12's HLSL doesn't have support for pointers
    // more info here: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#rawbufferload-and-rawbufferstore
    ShaderData shaderData = vk::RawBufferLoad<ShaderData>(
        vertexConstants.shaderDataStartAddress + sizeof(ShaderData) * vertexConstants.shaderDataIndex,
        16
    );

    float4x4 modelMatrix = shaderData.model;

    output.pos = mul(shaderData.projection, mul(shaderData.view, mul(modelMatrix, float4(input.inPosition.xyz, 1.0))));
    output.normal = mul(mul(shaderData.view, modelMatrix), input.inNormal);
    output.UV = input.inUV;

    float4 fragPos = mul(mul(shaderData.view, modelMatrix), float4(input.inPosition.xyz, 1.0));
    output.lightVec = shaderData.lightPos.xyz - fragPos.xyz;
    output.viewVec = -fragPos.xyz;

    output.lightingEnabled = shaderData.lightingEnabled;
    output.textureIndex = shaderData.textureIndex;

    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    if (input.lightingEnabled == 1) {
        float3 color = textureArray[input.textureIndex].Sample(samplerArray[input.textureIndex], input.UV).rgb;

        float3 N = normalize(input.normal.xyz);
        float3 L = normalize(input.lightVec);
        float3 V = normalize(input.viewVec);
        float3 R = reflect(-L, N);
        float3 diffuse = max(dot(N, L), 0.1);
        float3 specular = dot(N, L) > 0.0 ?
            pow(max(dot(R, V), 0.0), 16.0) * float3(0.75, 0.75, 0.75) * color.r :
            float3(0.0, 0.0, 0.0);

        return float4(diffuse * color.rgb + specular, 1.0);
    } else {
        float4 sampled = textureArray[input.textureIndex].Sample(samplerArray[input.textureIndex], input.UV);
        return float4(sampled.rgb, 1.0);
    }
}
