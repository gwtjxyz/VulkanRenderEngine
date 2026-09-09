module;

#ifdef DISABLE_IMPORT_STD
#include <memory>
#include <unordered_map>
#include <vector>
#endif

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

#include "core/util_macros.h"

#include <cassert>
#include <cstdint>

export module device_mapper;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import glm;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

import render_types;
import vulkan_resource_service;

// Structure to keep track of data mappings on the GPU
// Note: doesn't support downsizing for now, really - need to be careful about
// deleting and reusing resources inside these buffers
struct MappedBufferGroup {
    std::vector<VulkanShaderBufferData> shaderBuffers;

    uint32_t objectCount;                       // How many objects are currently used/allocated
    uint32_t maxObjectCount;                    // Maximum buffer capacity - if we ever exceed it, need to resize

    std::vector<bool> occupied;                 // true if object at that index exists, false otherwise

    explicit MappedBufferGroup() = default;

    explicit MappedBufferGroup(const std::vector<VulkanShaderBufferData> & shaderBufferData, const uint32_t maxObjectCount) :
        shaderBuffers(shaderBufferData),
        objectCount(0),
        maxObjectCount(maxObjectCount) {
        occupied.resize(maxObjectCount, false);
    }
};

export enum class MappedBufferType {
    Transform,
    Material,
    Draw,
    Light,
    Unknown
};

struct TransformLayout {
    glm::vec4 position;
    glm::vec4 rotation;
    glm::vec4 scale;
};

struct MaterialLayout {
    glm::vec4 materialTint { 0.0f };
    uint32_t textureIndex = INDEX_UNSET;            // texture is unset => use color only
    glm::vec3 unused;                               // padding
};

struct DrawLayout {
    uint32_t transformIndex = INDEX_UNSET;
    uint32_t materialIndex = INDEX_UNSET;
    hlsl_bool enabled = false;
};

struct LightLayout {
    uint32_t transformIndex = INDEX_UNSET;
    glm::vec3 color { 1.0f };
    float intensity = 1.0f;
    uint32_t enabled = false;
    // TODO add radius etc
};

// Layout = what we send to the GPU
// other params = everything else we need to track on the CPU side to enable rendering
// TODO simplify - no need for templating here
template <typename Layout>
struct DataBuffer {
    Layout layout {};
};

using TransformDataBuffer = DataBuffer<TransformLayout>;

using MaterialDataBuffer = DataBuffer<MaterialLayout>;

using DrawDataBuffer = DataBuffer<DrawLayout>;

using LightDataBuffer = DataBuffer<LightLayout>;

struct AddressMappings {
    vk::DeviceAddress drawAddress, lightAddress, transformAddress, materialAddress;
};

export class DeviceMapper;
GENERATE_LOCATOR(DeviceMapper)

// Handles mapping GPU buffers and syncing them with data stored on the CPU
class DeviceMapper {
public:
    DeviceMapper() = default;

    void preallocateBuffers(const uint32_t initialObjectCount, const uint32_t maxFramesInFlight) {
        m_MaxFramesInFlight = maxFramesInFlight;

        const auto * resourceService = VulkanResourceServiceLocator::locate();
        assert(resourceService != nullptr);

        m_MappedBuffers.clear();
        allocateBuffer(initialObjectCount, resourceService, MappedBufferType::Transform);
        allocateBuffer(initialObjectCount, resourceService, MappedBufferType::Material);
        allocateBuffer(initialObjectCount, resourceService, MappedBufferType::Draw);
        allocateBuffer(initialObjectCount, resourceService, MappedBufferType::Light);

        m_Transforms.resize(initialObjectCount, {});
        m_Materials.resize(initialObjectCount, {});
        m_Draws.resize(initialObjectCount, {});
        m_Lights.resize(initialObjectCount, {});
    }

    void freeResources() {
        for (auto & bufferPair : m_MappedBuffers) {
            for (auto i = 0; i < m_MaxFramesInFlight; ++i) {
                auto & bufferToFree = bufferPair.second.shaderBuffers[i];
                VulkanResourceServiceLocator::locate()->freeResourcesAndUnmapMemory(bufferToFree.buffer, bufferToFree.bufferMemory);
            }
        }
    }

    void uploadToGPU(const uint32_t frameIndex) {
        std::memcpy(
            m_MappedBuffers[MappedBufferType::Transform].shaderBuffers[frameIndex].mappedMemory,
            m_Transforms.data(),
            m_Transforms.size() * sizeof(TransformLayout)
        );
        std::memcpy(
            m_MappedBuffers[MappedBufferType::Material].shaderBuffers[frameIndex].mappedMemory,
            m_Materials.data(),
            m_Materials.size() * sizeof(MaterialLayout)
        );
        std::memcpy(
            m_MappedBuffers[MappedBufferType::Draw].shaderBuffers[frameIndex].mappedMemory,
            m_Draws.data(),
            m_Draws.size() * sizeof(DrawLayout)
        );
        std::memcpy(
            m_MappedBuffers[MappedBufferType::Light].shaderBuffers[frameIndex].mappedMemory,
            m_Lights.data(),
            m_Lights.size() * sizeof(LightLayout)
        );
    }

    AddressMappings getAddressMappingsForFrame(const uint32_t frameIndex) {
        AddressMappings result;
        result.drawAddress = m_MappedBuffers.at(MappedBufferType::Draw).shaderBuffers[frameIndex].bufferDeviceAddress;
        result.lightAddress = m_MappedBuffers.at(MappedBufferType::Light).shaderBuffers[frameIndex].bufferDeviceAddress;
        result.materialAddress = m_MappedBuffers.at(MappedBufferType::Material).shaderBuffers[frameIndex].bufferDeviceAddress;
        result.transformAddress = m_MappedBuffers.at(MappedBufferType::Transform).shaderBuffers[frameIndex].bufferDeviceAddress;

        return result;
    }

    uint32_t addTransform() {
        const uint32_t transformIndex = addToMappedBuffer(MappedBufferType::Transform);

        m_Transforms.at(transformIndex) = TransformDataBuffer();

        return transformIndex;
    }

    TransformDataBuffer * getTransform(const uint32_t transformIndex) {
        return &m_Transforms.at(transformIndex);
    }

    void removeTransform(const uint32_t transformIndex) {
        MappedBufferGroup & transformBuffer = m_MappedBuffers[MappedBufferType::Transform];

        transformBuffer.occupied.at(transformIndex) = false;
        transformBuffer.objectCount--;
    }

    uint32_t addMaterial(const glm::vec4 & materialTint, const uint32_t textureIndex) {
        const uint32_t materialIndex = addToMappedBuffer(MappedBufferType::Material);

        m_Materials.at(materialIndex) = { materialTint, textureIndex };

        return materialIndex;
    }

    MaterialDataBuffer * getMaterial(const uint32_t materialIndex) {
        return &m_Materials.at(materialIndex);
    }

    void removeMaterial(const uint32_t materialIndex) {
        MappedBufferGroup & materialBuffer = m_MappedBuffers[MappedBufferType::Material];

        materialBuffer.occupied.at(materialIndex) = false;
        materialBuffer.objectCount--;
    }

    uint32_t addDraw() {
        const uint32_t drawIndex = addToMappedBuffer(MappedBufferType::Draw);

        m_Draws.at(drawIndex) = { INDEX_UNSET, INDEX_UNSET, true };

        return drawIndex;
    }

    DrawDataBuffer * getDraw(const uint32_t drawIndex) {
        return &m_Draws.at(drawIndex);
    }

    void removeDraw(const uint32_t drawIndex) {
        MappedBufferGroup & drawBuffer = m_MappedBuffers[MappedBufferType::Draw];

        drawBuffer.occupied.at(drawIndex) = false;
        drawBuffer.objectCount--;
    }

    uint32_t addLight() {
        const uint32_t lightIndex = addToMappedBuffer(MappedBufferType::Light);

        m_Lights.at(lightIndex) = LightDataBuffer();

        return lightIndex;
    }

    LightDataBuffer * getLight(const uint32_t lightIndex) {
        return &m_Lights.at(lightIndex);
    }

    void removeLight(const uint32_t lightIndex) {
        MappedBufferGroup & lightBuffer = m_MappedBuffers[MappedBufferType::Light];

        lightBuffer.occupied.at(lightIndex) = false;
        lightBuffer.objectCount--;
    }

    uint32_t getLightCount() const {
        return m_Lights.size();
    }

private:
    static uint32_t getLayoutSize(MappedBufferType type) {
        switch (type) {
            case MappedBufferType::Transform:
                return sizeof(TransformLayout);
            case MappedBufferType::Material:
                return sizeof(MaterialLayout);
            case MappedBufferType::Draw:
                return sizeof(DrawLayout);
            case MappedBufferType::Light:
                return sizeof(LightLayout);
            default:
                throw std::runtime_error("Unknown buffer layout.");
        }
    }

    void allocateBuffer(const uint32_t initialObjectCount, const VulkanResourceService * resourceService, MappedBufferType bufferType) {
        std::vector<VulkanShaderBufferData> shaderBufferDataVector {};
        for (auto i = 0; i < m_MaxFramesInFlight; ++i) {
            auto shaderBufferData = resourceService->createShaderBuffer(initialObjectCount * getLayoutSize(bufferType));
            shaderBufferDataVector.emplace_back(shaderBufferData);
        }
        MappedBufferGroup bufferGroup(shaderBufferDataVector, initialObjectCount);
        m_MappedBuffers.emplace(bufferType, std::move(bufferGroup));
    }

    // Note: Currently, we'd have to vkDeviceWaitIdle() to perform this, otherwise bad things can happen
    void resizeBuffer(const uint32_t newMaxObjectCount, const MappedBufferType bufferToResize) {
        MappedBufferGroup oldMappedBuffer = m_MappedBuffers[bufferToResize];
        // TODO support downsizing as well
        assert(oldMappedBuffer.maxObjectCount < newMaxObjectCount);

        const auto resourceService = VulkanResourceServiceLocator::locate();

        std::vector<VulkanShaderBufferData> newBufferData {};
        for (uint32_t i = 0; i < m_MaxFramesInFlight; ++i) {
            auto newBuffer = resourceService->createShaderBuffer(newMaxObjectCount * getLayoutSize(bufferToResize));
            resourceService->copyBuffer(oldMappedBuffer.shaderBuffers[i].buffer, newBuffer.buffer, oldMappedBuffer.maxObjectCount * getLayoutSize(bufferToResize));

            resourceService->freeResourcesAndUnmapMemory(oldMappedBuffer.shaderBuffers[i].buffer, oldMappedBuffer.shaderBuffers[i].bufferMemory);
            newBufferData.emplace_back(newBuffer);
        }

        MappedBufferGroup newMappedBufferGroup(newBufferData, newMaxObjectCount);

        // Copy old object occupancy data over
        for (
            auto newIt = newMappedBufferGroup.occupied.begin(), oldIt = oldMappedBuffer.occupied.begin();
            oldIt != oldMappedBuffer.occupied.end();
            ++newIt, ++oldIt
        ) {
            *newIt = *oldIt;
        }

        m_MappedBuffers[bufferToResize] = newMappedBufferGroup;
    }

    uint32_t addToMappedBuffer(const MappedBufferType bufferType) {
        uint32_t bufferDataIndex = 0;
        MappedBufferGroup & mappedDataBuffer = m_MappedBuffers[bufferType];

        for (const auto occupiedAtIndex : mappedDataBuffer.occupied) {
            if (!occupiedAtIndex) {
                break;
            }
            bufferDataIndex++;
        }

        mappedDataBuffer.objectCount++;
        if (mappedDataBuffer.objectCount >= mappedDataBuffer.maxObjectCount) {
            auto newMaxObjectCount = mappedDataBuffer.maxObjectCount + 100;
            resizeBuffer(newMaxObjectCount, bufferType);
            mappedDataBuffer = m_MappedBuffers[bufferType];

            switch (bufferType) {
                case MappedBufferType::Transform:
                    m_Transforms.resize(mappedDataBuffer.maxObjectCount, {});
                    break;
                case MappedBufferType::Material:
                    m_Materials.resize(mappedDataBuffer.maxObjectCount, {});
                    break;
                case MappedBufferType::Draw:
                    m_Draws.resize(mappedDataBuffer.maxObjectCount, {});
                    break;
                case MappedBufferType::Light:
                    m_Lights.resize(mappedDataBuffer.maxObjectCount, {});
                    break;
                default:
                    throw std::runtime_error("Trying to resize an unknown buffer type, something has gone terribly wrong!");
            }
        }

        mappedDataBuffer.occupied.at(bufferDataIndex) = true;

        return bufferDataIndex;
    }

private:
    std::unordered_map<MappedBufferType, MappedBufferGroup> m_MappedBuffers {};
    uint32_t m_MaxFramesInFlight = 0;

    // Not sure if this should be in this class or somewhere else
    std::vector<TransformDataBuffer> m_Transforms {};
    std::vector<MaterialDataBuffer> m_Materials {};
    std::vector<DrawDataBuffer> m_Draws {};
    std::vector<LightDataBuffer> m_Lights {};
};
