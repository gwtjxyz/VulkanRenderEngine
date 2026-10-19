#include "common.hlsli"
#include "vertex_push_constants.hlsli"

struct VSInput {
    float3 inPosition: POSITION0;
    float3 inNormal: NORMAL0;
    float2 inUV: TEXCOORD0;
};

[[vk::binding(0)]] SamplerState textureSampler;
[[vk::binding(1)]] Texture2D textureArray[];

#define LIGHTING_MODE_OFF 0
#define LIGHTING_MODE_PHONG 1
#define LIGHTING_MODE_GOOCH 2

// Obviously very inefficient (storing flags as uints instead of bits), but for just toying around it's not a big deal
struct VSOutput {
    float4 pos : SV_POSITION;
    float4 normal : NORMAL0;
    float2 UV: TEXCOORD0;
    float4 fragPos : FRAG_POS;
    int lightMode : LIGHT_MODE;

    uint64_t materialAddress : MATERIAL_ADDRESS;
    uint materialIndex : MATERIAL_INDEX;

    uint64_t transformAddress : TRANSFORM_ADDRESS;
    uint64_t lightAddress : LIGHT_ADDRESS;
    uint lightCount : LIGHT_COUNT;

    bool drawing : DRAWING;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    // First, check if we actually need to draw anything
    // for entities without an active draw component, drawIndex will be of a special value

    if (vertexConstants.drawIndex == UINT32_MAX) {
        output.drawing = false;
        return output;
    }

    // DXC's way of supporting buffer_device_address extension, since DX12's HLSL doesn't have support for pointers
    // more info here: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#rawbufferload-and-rawbufferstore
    DrawLayout drawData = rawVkBufferLoad<DrawLayout>(vertexConstants.drawAddress, vertexConstants.drawIndex);

    // Check if the entity we're trying to draw has enough data for us to work with, if not, return early
    if (!drawData.enabled || drawData.transformIndex == UINT32_MAX || drawData.materialIndex == UINT32_MAX) {
        output.materialIndex = drawData.materialIndex;
        output.drawing = false;
        return output;
    }

    output.drawing = true;

    TransformLayout transformData = rawVkBufferLoad<TransformLayout>(vertexConstants.transformAddress, drawData.transformIndex);
    float4x4 modelMatrix = buildModelMatrix(transformData.position, transformData.rotation, transformData.scale);

    output.pos = mul(vertexConstants.projection, mul(vertexConstants.view, mul(modelMatrix, float4(input.inPosition.xyz, 1.0))));
    output.normal = mul(mul(vertexConstants.view, modelMatrix), float4(input.inNormal, 0.0));
    output.UV = input.inUV;

    output.fragPos = mul(mul(vertexConstants.view, modelMatrix), float4(input.inPosition.xyz, 1.0));
    // Do we need fragment push constants now too for this?
    // Actually can just send it over to PSMain from here
    // and then inside PSMain we will do something like
    //
    // float4 output;
    // for (int i = 0; i < lightCount; ++i) {
    //   output += lighting(...);
    // }
    // output.lightVec = (shaderData.lightPos.xyz - fragPos.xyz);
    // output.viewVec = -fragPos.xyz;

    output.lightMode = vertexConstants.lightMode;

    output.transformAddress = vertexConstants.transformAddress;
    output.materialAddress = vertexConstants.materialAddress;
    output.materialIndex = drawData.materialIndex;

    output.lightAddress = vertexConstants.lightAddress;
    output.lightCount = vertexConstants.lightCount;

    return output;
}

float4 noLighting(float4 color) {
    return color;
}

float4 phongLighting(float4 color, float3 normal, float3 light, float3 view) {
    float3 N = normalize(normal);
    float3 L = normalize(light);
    float3 V = normalize(view);
    float3 R = reflect(-L, N);

    float3 diffuse = max(dot(N, L), 0.0);
    float3 specular = dot(N, L) > 0.0 ?
        pow(max(dot(R, V), 0.0), 16.0) * float3(0.75, 0.75, 0.75) * color.r :
        float3(0.0, 0.0, 0.0);

    return float4(diffuse * color.rgb + specular, 1.0);
}

float4 goochLighting(float4 color, float3 normal, float3 light, float3 view) {
    float3 surface = color.rgb;

    float3 one = float3(1.0, 1.0, 1.0);

    float3 cool = float3(0.0, 0.0, 0.55) + surface * 0.25;
    float3 warm = float3(0.3, 0.3, 0.0) + surface * 0.25;
    float3 highlight = one;

    float3 N = normalize(normal);
    float3 L = normalize(light);
    float3 V = normalize(view);

    float3 T = (dot(N, L) + one) * 0.5;
    float3 R = reflect(-L, N); // same as 2 * N * dot(N, L) - L
    float3 S = clamp(dot(R, V) * 100 - float3(97.0, 97.0, 97.0), 0.0, 1.0);

    // formula without using lerp()
    // float3 shaded = S * highlight + (one - S) * (T * warm + (one - T) * cool);
    // formula with using lerp()
    float3 warmCoolInterp = lerp(cool, warm, T);
    float3 shaded = lerp(warmCoolInterp, highlight, S);

    return float4(shaded, 1.0);
}

float4 PSMain(VSOutput input) : SV_TARGET {
    // Check if we need to draw at all
    if (!input.drawing) {
        discard;
    }

    // Load material for current fragment
    MaterialLayout materialData = rawVkBufferLoad<MaterialLayout>(input.materialAddress, input.materialIndex);

    float4 color;
    // If texture is set, use texture for color
    if (materialData.textureIndex != UINT32_MAX) {
        // Using descriptor indexing = need to use non uniform index to make sure everything works correctly
        uint textureIndex = NonUniformResourceIndex(materialData.textureIndex);
        color = float4(textureArray[textureIndex].Sample(textureSampler, input.UV));
    } else {
        color = materialData.materialTint;
    }

    if (input.lightMode == LIGHTING_MODE_OFF) {
        return noLighting(color);
    }

    // Ambient (default) light
    color *= 0.4f;

    // Iterate through all lights and apply them to the fragment
    for (uint i = 0; i < input.lightCount; ++i) {
        LightLayout lightData = rawVkBufferLoad<LightLayout>(input.lightAddress, i);

        if (!lightData.enabled || lightData.transformIndex == UINT32_MAX) continue;

        TransformLayout transformData = rawVkBufferLoad<TransformLayout>(input.transformAddress, lightData.transformIndex);
        float3 lightVec = transformData.position.xyz - input.fragPos.xyz;
        float3 viewVec = -input.fragPos.xyz;
        if (input.lightMode == LIGHTING_MODE_PHONG) {
            color += phongLighting(color, input.normal.xyz, lightVec, viewVec) * lightData.intensity;
        } else if (input.lightMode == LIGHTING_MODE_GOOCH) {
            color += goochLighting(color, input.normal.xyz, lightVec, viewVec) * lightData.intensity;
        }
    }

    return color;
}
