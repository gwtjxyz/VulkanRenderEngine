struct VSInput {
    [[vk::location(0)]] float4 inPosition : SV_POSITION;
    [[vk::location(1)]] float4 inColor : COLOR0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    [[vk::builtin("PointSize")]] float pointSize : POINTSIZE;
    float4 fragColor : COLOR0;
};

VSOutput VSMain(VSInput vertexInput) {
    VSOutput output;

    output.pointSize = 12.8;
    output.pos = vertexInput.inPosition;
    output.fragColor = vertexInput.inColor;

    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    return float4(input.fragColor);
}
