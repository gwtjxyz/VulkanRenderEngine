#include "vertex_push_constants.hlsl"

struct VSInput {
    [[vk::location(0)]] float4 inPosition : SV_POSITION;
    [[vk::location(1)]] float4 inColor : COLOR0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    [[vk::builtin("PointSize")]] float pointSize : POINTSIZE;
    float3 fragColor : COLOR0;
    float2 pointCoord : TEXCOORD0;
    uint enabled : ENABLED;
};

VSOutput VSMain(VSInput vertexInput) {
    VSOutput output;
    output.enabled = vertexConstants.particlesEnabled;

    if (vertexConstants.particlesEnabled == 0) {
        return output;
    }

    output.pointSize = 14.0;
    output.pos = vertexInput.inPosition;
    output.fragColor = vertexInput.inColor.rgb;
    output.pointCoord = (vertexInput.inPosition.xy * float2(1.0, -1.0) + float2(1.0, 1.0)) / 2.0;

    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    if (input.enabled == 0) discard;

    float2 coord = input.pointCoord - float2(0.5, 0.5);
    return float4(input.fragColor, 0.5 - length(coord));
}
