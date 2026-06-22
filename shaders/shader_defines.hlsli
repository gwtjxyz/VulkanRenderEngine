#ifndef SHADER_DEFINES_HLSLI
#define SHADER_DEFINES_HLSLI

#define UINT32_MAX 0xffffffffu
#define UINT64_MAX 0xffffffffffffffffu

template<typename T>
T rawVkBufferLoad(uint64_t bufferAddress, uint index) {
    return vk::RawBufferLoad<T>(
        bufferAddress + sizeof(T) * index,
        4
    );
}

#endif // SHADER_DEFINES_HLSLI
