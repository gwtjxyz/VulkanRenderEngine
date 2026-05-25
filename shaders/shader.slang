struct VSInput {
    float4 inPosition; // implicitly location 0
    float4 inNormal;    // implicitly location 1
    float2 inUV; // implicitly location 2
}

Sampler2D textures[];

struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model;
    float4 lightPos;
    uint32_t lightingEnabled;
    uint32_t textureIndex;
}

struct VSOutput {
    float4 pos : SV_Position;
    float4 normal;
    float2 UV;
    float3 lightVec;
    float3 viewVec;
    uint32_t lightingEnabled;
    uint32_t textureIndex;
};

[shader("vertex")]
VSOutput vertMain(VSInput input, uniform ShaderData * shaderData) {
    VSOutput output;
    float4x4 modelMatrix = shaderData->model;

    output.pos = mul(shaderData->projection, mul(shaderData->view, mul(modelMatrix, float4(input.inPosition.xyz, 1.0))));
    output.normal = mul(mul(shaderData->view, modelMatrix), input.inNormal);
    output.UV = input.inUV;

    // Calculate view vectors required for lighting
    float4 fragPos = mul(mul(shaderData->view, modelMatrix), float4(input.inPosition.xyz, 1.0));
    output.lightVec = shaderData->lightPos.xyz - fragPos.xyz;
    output.viewVec = -fragPos.xyz;

    output.lightingEnabled = shaderData.lightingEnabled;
    output.textureIndex = shaderData.textureIndex;

    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    if (vertIn.lightingEnabled == 1) {
        // Phong lighting
        float3 N = normalize(vertIn.normal.xyz);
        float3 L = normalize(vertIn.lightVec);
        float3 V = normalize(vertIn.viewVec);
        float3 R = reflect(-L, N);
        float3 diffuse = max(dot(N, L), 0.0025);
        float3 specular = pow(max(dot(R, V), 0.0), 16.0) * 0.75;

        // Sample from texture
        float3 color = textures[vertIn.textureIndex].Sample(vertIn.UV).rgb;

        return float4(diffuse * color.rgb + specular, 1.0);
    } else {
        float4 sampled = textures[vertIn.textureIndex].Sample(vertIn.UV);
        return float4(sampled.rgb, 1.0);
    }
}
