#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#define UINT32_MAX 0xffffffffu
#define UINT64_MAX 0xffffffffffffffffu

template<typename T>
T rawVkBufferLoad(uint64_t bufferAddress, uint index) {
    return vk::RawBufferLoad<T>(
        bufferAddress + sizeof(T) * index,
        4
    );
}

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
    float4x4 result = mat;
    result[0][0] *= v.x;
    result[1][1] *= v.y;
    result[2][2] *= v.z;

    return result;
}

// Interpret transform as 3 float4 vectors
float4x4 buildModelMatrix(float4 position, float4 rotation, float4 scale) {
    float4x4 T = transMat(identity(), position);
    float4x4 R = quatToRotMat(rotation);
    float4x4 S = scaleMat(identity(), scale);

    return mul(T, mul(R, S));
}

#endif // COMMON_HLSLI