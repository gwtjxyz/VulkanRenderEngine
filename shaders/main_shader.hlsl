#include "vertex_push_constants.hlsli"
#include "shader_defines.hlsli"

#pragma pack_matrix( row_major )

struct VSInput {
    float4 inPosition: POSITION0;
    float4 inNormal: NORMAL0;
    float2 inUV: TEXCOORD0;
};

[[vk::binding(0)]] SamplerState textureSampler;
[[vk::binding(1)]] Texture2D textureArray[];

#define LIGHTING_MODE_OFF 0
#define LIGHTING_MODE_PHONG 1
#define LIGHTING_MODE_GOOCH 2

float4x4 identity() {
    return float4x4(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
}

float4x4 transMat(float4x4 mat, float4 v) {
    float4x4 result = mat;
    result[0][3] = v.x;
    result[1][3] = v.y;
    result[2][3] = v.z;

    return result;
}

float4x4 quatToRotMat(float4 q) {
    // Written using GLM as reference
    float4x4 result = identity();

    float qxx = pow(q.x, 2);
    float qyy = pow(q.y, 2);
    float qzz = pow(q.z, 2);
    float qxz = q.x * q.z;
    float qxy = q.x * q.y;
    float qyz = q.y * q.z;
    float qwx = q.w * q.x;
    float qwy = q.w * q.y;
    float qwz = q.w * q.z;

#if 0
    // row-major
    result[0][0] = 1.0 - 2.0 * (qyy + qzz);
    result[0][1] = 2.0 * (qxy + qwz);
    result[0][2] = 2.0 * (qxz - qwy);

    result[1][0] = 2.0 * (qxy - qwz);
    result[1][1] = 1.0 - 2.0 * (qxx + qzz);
    result[1][2] = 2.0 * (qyz + qwx);

    result[2][0] = 2.0 * (qxz + qwy);
    result[2][1] = 2.0 * (qyz - qwx);
    result[2][2] = 1.0 - 2.0 * (qxx + qyy);
#else
    // column-major
    result[0][0] = 1.0 - 2.0 * (qyy + qzz);
    result[1][0] = 2.0 * (qxy + qwz);
    result[2][0] = 2.0 * (qxz - qwy);

    result[0][1] = 2.0 * (qxy - qwz);
    result[1][1] = 1.0 - 2.0 * (qxx + qzz);
    result[2][1] = 2.0 * (qyz + qwx);

    result[0][2] = 2.0 * (qxz + qwy);
    result[1][2] = 2.0 * (qyz - qwx);
    result[2][2] = 1.0 - 2.0 * (qxx + qyy);
#endif
    return result;
}

float4x4 scaleMat(float4x4 mat, float4 v) {
    float4x4 result;
    // TODO double-check this math - not sure why it works atm
    result[0] = mat[0] * v[0];
    result[1] = mat[1] * v[1];
    result[2] = mat[2] * v[2];
    result[3] = mat[3];
    return result;
}

// Pretend transform is stored as 3 float4 vectors
float4x4 buildModelMatrix(float4 position, float4 rotation, float4 s) {
    float4x4 T = transMat(identity(), position);
    float4x4 R = quatToRotMat(rotation);
    float4x4 S = scaleMat(identity(), s);

    return mul(T, mul(R, S));
}

// Obviously very inefficient (storing flags as uints instead of bits), but for just toying around it's not a big deal
struct VSOutput {
    float4 pos : SV_POSITION;
    float4 normal : NORMAL0;
    float2 UV: TEXCOORD0;
    float4 fragPos : FRAGPOS;
    int lightMode : LIGHTMODE;

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
    // Do I actually need both projection and view matrices?
    output.normal = mul(mul(vertexConstants.view, modelMatrix), input.inNormal);
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

    float3 diffuse = max(dot(N, L), 0.1);
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

/*
    float4 pos : SV_POSITION;
    float4 normal : NORMAL0;
    float2 UV: TEXCOORD0;
    float4 fragPos : FRAGPOS;
    int lightMode : LIGHTMODE;

    uint64_t materialAddress : MATERIAL_ADDRESS;
    uint materialIndex : MATERIAL_INDEX;

    uint64_t transformAddress : TRANSFORM_ADDRESS;
    uint64_t lightAddress : LIGHTPTR;
    uint lightCount : LIGHTCOUNT;

    bool drawing : DRAWING;
*/

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

    // Iterate through all lights and apply them to the fragment
    for (uint i = 0; i < input.lightCount; ++i) {
        LightLayout lightData = rawVkBufferLoad<LightLayout>(input.lightAddress, i);

        if (!lightData.enabled || lightData.transformIndex == UINT32_MAX) continue;

        TransformLayout transformData = rawVkBufferLoad<TransformLayout>(input.transformAddress, lightData.transformIndex);
        float3 lightVec = transformData.position.xyz - input.fragPos.xyz;
        float3 viewVec = -input.fragPos.xyz;
        if (input.lightMode == LIGHTING_MODE_PHONG) {
            color = phongLighting(color, input.normal.xyz, lightVec, viewVec);
        } else if (input.lightMode == LIGHTING_MODE_GOOCH) {
            color = goochLighting(color, input.normal.xyz, lightVec, viewVec);
        }
    }

    return color;
}
