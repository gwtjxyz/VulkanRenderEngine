#include "common.hlsli"
#include "vertex_push_constants.hlsli"

struct VSInput {
    float3 inPosition: POSITION0;
    float3 inNormal: NORMAL0;
    float2 inUV: TEXCOORD0;
};

[[vk::binding(0)]] SamplerState textureSampler;
[[vk::binding(1)]] Texture2D textureArray[];

// TODO: replace with static constants instead of defines?
#define LIGHTING_MODE_OFF 0
#define LIGHTING_MODE_PHONG 1
#define LIGHTING_MODE_GOOCH 2

static const float gMinLightDst = 0.5f;
static const float gMaxLightDst = 200.0f;

float distanceMultiplier(float distance, float intensity) {
    float multiplier = intensity / max(distance, gMinLightDst);
    return multiplier * multiplier;
}

// Obviously very inefficient (storing flags as uints instead of bits), but for just toying around it's not a big deal
struct VSOutput {
    float4 pos : SV_POSITION;                       // Vertex position
    float4 normal : NORMAL0;                        // Vertex normal
    float2 UV: TEXCOORD0;
    float4 cameraPos : CAMERA_POS;
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

    if (vertexConstants.drawIndex == UINT32_MAX || vertexConstants.instanceTransformIndex == UINT32_MAX) {
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

    // Scene transform = transform of any given asset
    // Instance transform = transform of any given instance using a particular asset
    // To properly place something in the world, we need to combine both.
    TransformLayout sceneTransformData = rawVkBufferLoad<TransformLayout>(vertexConstants.transformAddress, drawData.transformIndex);
    float4x4 sceneMatrix = buildModelMatrix(sceneTransformData.position, sceneTransformData.rotation, sceneTransformData.scale);
    TransformLayout instanceTransformData = rawVkBufferLoad<TransformLayout>(vertexConstants.transformAddress, vertexConstants.instanceTransformIndex);
    float4x4 instanceMatrix = buildModelMatrix(instanceTransformData.position, instanceTransformData.rotation, instanceTransformData.scale);

    float4x4 modelMatrix = mul(instanceMatrix, sceneMatrix);

    // Move vertex position into camera space
    output.pos = mul(vertexConstants.projection, mul(vertexConstants.view, mul(modelMatrix, float4(input.inPosition.xyz, 1.0))));
    // Move normal into world space
    // Don't need to do inverse-transpose or similar corrections because we
    // currently do not apply non-uniform scaling or shearing transforms
    output.normal = normalize(mul(modelMatrix, float4(input.inNormal, 0.0)));   // Note: can also normalize by dividing by the scale factor
//    output.normal = mul(modelMatrix, float4(input.inNormal, 0.0)) / instanceTransformData.scale.x;
    output.UV = input.inUV;

    // Camera pos is already in world space - no transformations needed
    output.cameraPos = vertexConstants.cameraPos;

    // Move fragment/pixel position into world space
    output.fragPos = mul(modelMatrix, float4(input.inPosition.xyz, 1.0));

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

// Implementation of Gooch lighting that supports more than one light source
float3 goochMultiLight(
    uint lightCount,
    uint64_t lightAddress, uint64_t transformAddress,
    float4 cameraPos, float4 fragPos,
    float4 surfaceColor,
    float4 normal
) {
    float3 cool = float3(0.0, 0.0, 0.55) + 0.25 * surfaceColor.rgb;
    float3 warm = float3(0.3, 0.3, 0) +  0.25 * surfaceColor.rgb;
    float3 highlight = float3(2.0, 2.0, 2.0);

    float3 outColor = 0.5 * cool;

    for (uint i = 0; i < lightCount; ++i) {
        LightLayout lightData = rawVkBufferLoad<LightLayout>(lightAddress, i);

        if (!lightData.enabled || lightData.transformIndex == UINT32_MAX) continue;

        TransformLayout lightTransformData = rawVkBufferLoad<TransformLayout>(transformAddress, lightData.transformIndex);

        float4 N = normalize(normal);
        float4 V = normalize(cameraPos - fragPos);
        float4 L = normalize(lightTransformData.position - fragPos);

        float4 r_l = reflect(-L, N);
        float s = saturate(100 * dot(r_l, V) - 97.0);

        float NdL = saturate(dot(N, L));
        float3 lit = lerp(warm, highlight, s);

        outColor += NdL * lightData.color * lightData.intensity * lit;
    }

    return outColor;
}

// Implementation of Phong lighting that supports more than one light source
float3 phongMultilight(
    uint lightCount,
    uint64_t lightAddress, uint64_t transformAddress,
    float4 cameraPos, float4 fragPos,
    float4 surfaceColor,
    float4 normal
) {
    float3 outColor = surfaceColor.rgb * 0.4; // Ambient (pre-defined for now)

    for (uint i = 0; i < lightCount; ++i) {
        LightLayout lightData = rawVkBufferLoad<LightLayout>(lightAddress, i);

        if (!lightData.enabled || lightData.transformIndex == UINT32_MAX) continue;

        TransformLayout lightTransformData = rawVkBufferLoad<TransformLayout>(transformAddress, lightData.transformIndex);

        float4 N = normalize(normal);
        float4 V = normalize(cameraPos - fragPos);
        float4 L = normalize(lightTransformData.position - fragPos);

        float4 r_l = reflect(-L, N);

        // Pre-define specular and diffuse values as same for every light for now
        // In the future use materials / diffuse textures / etc for this instead
        float diffuseIntensity = 1.0;
        float specularIntensity = 1.0;
        float specularCoefficient = 16;

        float3 diffuse = max(dot(N, L), 0.0) * diffuseIntensity;

        float3 specular = specularIntensity * dot(r_l, V) > 0.0 ?
            pow(dot(r_l, V), specularCoefficient) :
            float3(0.0, 0.0, 0.0);

        float3 intensities = lightData.intensity * lightData.color * (diffuse + specular);
        outColor += surfaceColor.rgb * intensities;
    }

    return outColor;
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
    color.rgb *= 0.4f;

    if (input.lightMode == LIGHTING_MODE_GOOCH) {
        color.rgb = goochMultiLight(input.lightCount, input.lightAddress, input.transformAddress, input.cameraPos, input.fragPos, color, input.normal);

        return color;
    } else if (input.lightMode == LIGHTING_MODE_PHONG) {
        color.rgb = phongMultilight(input.lightCount, input.lightAddress, input.transformAddress, input.cameraPos, input.fragPos, color, input.normal);

        return color;
    }

    return color;
}
