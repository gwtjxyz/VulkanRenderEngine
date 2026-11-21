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

import core_types;
import render_types;
import vulkan_resource_service;

export enum class BufferType {
    Transform,
    Material,
    Draw,
    Light,
    Unknown
};

struct TransformLayout {
    glm::vec4 position {};
    glm::vec4 rotation {};
    glm::vec4 scale {};
};

struct MaterialLayout {
    glm::vec4 materialTint { 0.0f };
    uint32_t textureIndex = INDEX_UNSET;            // texture is unset => use color only
    glm::vec3 unused {};                            // padding
};

struct DrawLayout {
    uint32_t transformIndex = INDEX_UNSET;
    uint32_t materialIndex = INDEX_UNSET;
    hlsl_bool enabled = false;                      // booleans in HLSL are 4 bytes
};

struct LightLayout {
    uint32_t transformIndex = INDEX_UNSET;
    glm::vec3 color { 1.0f };
    float intensity = 1.0f;
    hlsl_bool enabled = false;
};

struct AddressMappings {
    vk::DeviceAddress transformAddress, materialAddress, drawAddress, lightAddress;
};

struct CopyCommand {
    BufferType type;
    uint32_t start;
    uint32_t end;
    uint32_t frameIndex;
};

template <typename T>
struct CPUBuffer {
    std::vector<T> data {};
    std::vector<bool> dirty {};
};

// Structure to keep track of data mappings on the GPU
// Note: doesn't support downsizing for now - need to be careful about
// deleting and reusing resources inside these buffers
struct GPUBufferGroup {
    std::vector<VulkanBDABufferData> bdaBuffers {}; // One per frame in flight

    uint32_t objectCount = 0;                        // How many objects are currently used/allocated
    uint32_t maxObjectCount = 0;                     // Maximum buffer capacity - if we ever exceed it, need to resize

    std::vector<bool> occupied {};                   // true if object at that index exists, false otherwise
};

template <typename T>
class DeviceBuffer {
public:
    DeviceBuffer() = default;

    void initialize(const uint32_t initialObjectCount, const uint32_t maxFramesInFlight) {
        m_CpuBuffer.data.resize(initialObjectCount, {});
        m_CpuBuffer.dirty.resize(initialObjectCount, true);

        VulkanResourceService * vulkanResourceService = VulkanResourceServiceLocator::locate();

        for (auto i = 0; i < maxFramesInFlight; ++i) {
            auto bdaBuffer = vulkanResourceService->createBDABuffer(initialObjectCount * sizeof(T));
            m_GpuBufferGroup.bdaBuffers.emplace_back(bdaBuffer);
        }

        m_GpuBufferGroup.maxObjectCount = initialObjectCount;
        m_GpuBufferGroup.occupied.resize(initialObjectCount, false);
    }

    void destroy() {
        VulkanResourceService * vulkanResourceService = VulkanResourceServiceLocator::locate();

        for (auto & b : m_GpuBufferGroup.bdaBuffers) {
            vulkanResourceService->freeResourcesAndUnmapMemory(b.buffer, b.bufferMemory);
        }
        m_GpuBufferGroup.bdaBuffers.clear();
        m_GpuBufferGroup.objectCount = 0;
        m_GpuBufferGroup.maxObjectCount = 0;
        m_GpuBufferGroup.occupied.clear();

        m_CpuBuffer.data.clear();
        m_CpuBuffer.dirty.clear();
    }

    uint32_t add() {
        uint32_t firstFreeIndex = 0;

        for (const auto occupiedAtIndex : m_GpuBufferGroup.occupied) {
            if (!occupiedAtIndex) {
                break;
            }
            firstFreeIndex++;
        }

        m_GpuBufferGroup.objectCount++;
        if (m_GpuBufferGroup.objectCount >= m_GpuBufferGroup.maxObjectCount) {
            const uint32_t newMaxObjectCount = m_GpuBufferGroup.maxObjectCount + 100;
            expand(newMaxObjectCount);
        }

        m_GpuBufferGroup.occupied.at(firstFreeIndex) = true;
        m_CpuBuffer.dirty.at(firstFreeIndex) = true;            // TODO not sure if need to mark dirty here

        return firstFreeIndex;
    }

    // We always mark dirty because we assume that the caller will only request layout data in order to modify it
    T & getAndMarkDirty(const uint32_t index) {
        m_CpuBuffer.dirty.at(index) = true;
        return m_CpuBuffer.data.at(index);
    }

    void remove(const uint32_t index) {
        m_GpuBufferGroup.occupied.at(index) = false;
        m_CpuBuffer.dirty.at(index) = true;
    }

    vk::DeviceAddress getBufferDeviceAddressForFrame(const uint32_t frameIndex) const {
        return m_GpuBufferGroup.bdaBuffers.at(frameIndex).bufferDeviceAddress;
    }

    uint32_t getObjectCount() const {
        return m_CpuBuffer.data.size();
    }

    std::vector<bool> & getDirtyStates() {
        return m_CpuBuffer.dirty;
    }

    T * getCpuSideData() {
        return m_CpuBuffer.data.data();
    }

    void * getMappedMemory(const uint32_t frameIndex) const {
        return m_GpuBufferGroup.bdaBuffers.at(frameIndex).mappedMemory;
    }

    static size_t getLayoutSize() {
        return sizeof(T);
    }

private:
    // Have to do this while Vulkan device is idle, otherwise bad things can happen
    void expand(const uint32_t newMaxObjectCount) {
        assert(newMaxObjectCount > m_GpuBufferGroup.maxObjectCount && "Must be expanding to a size bigger than the current size.");

        auto * vulkanResourceService = VulkanResourceServiceLocator::locate();
        const uint32_t maxFramesInFlight = m_GpuBufferGroup.bdaBuffers.size();

        m_CpuBuffer.data.resize(newMaxObjectCount, {});
        m_CpuBuffer.dirty.resize(newMaxObjectCount, true);

        std::vector<VulkanBDABufferData> newBdaBuffers {};
        for (auto i = 0; i < maxFramesInFlight; ++i) {
            auto newBdaBuffer = vulkanResourceService->createBDABuffer(newMaxObjectCount * sizeof(T));
            vulkanResourceService->copyBuffer(
                m_GpuBufferGroup.bdaBuffers[i].buffer,
                newBdaBuffer.buffer,
                m_GpuBufferGroup.maxObjectCount * sizeof(T)
            );

            vulkanResourceService->freeResourcesAndUnmapMemory(m_GpuBufferGroup.bdaBuffers[i].buffer, m_GpuBufferGroup.bdaBuffers[i].bufferMemory);
            newBdaBuffers.emplace_back(newBdaBuffer);
        }

        m_GpuBufferGroup.bdaBuffers = std::move(newBdaBuffers);
        m_GpuBufferGroup.occupied.resize(newMaxObjectCount, false);
        m_GpuBufferGroup.maxObjectCount = newMaxObjectCount;
    }

private:
    CPUBuffer<T> m_CpuBuffer {};
    GPUBufferGroup m_GpuBufferGroup {};
};

export class DeviceMapper;
GENERATE_LOCATOR(DeviceMapper)

// Handles mapping GPU buffers and syncing them with data stored on the CPU
// TODO simplify allocations etc
class DeviceMapper {
public:
    DeviceMapper() = default;

    void preallocateBuffers(const uint32_t initialObjectCount, const uint32_t maxFramesInFlight) {
        m_MaxFramesInFlight = maxFramesInFlight;

        const auto * resourceService = VulkanResourceServiceLocator::locate();
        assert(resourceService != nullptr);

        m_Transforms.initialize(initialObjectCount, m_MaxFramesInFlight);
        m_Materials.initialize(initialObjectCount, m_MaxFramesInFlight);
        m_Draws.initialize(initialObjectCount, m_MaxFramesInFlight);
        m_Lights.initialize(initialObjectCount, m_MaxFramesInFlight);
    }

    void freeResources() {
        m_Transforms.destroy();
        m_Materials.destroy();
        m_Draws.destroy();
        m_Lights.destroy();
    }

    void prepareCopyCommands() {
        prepareCopyCommandsForBuffer(BufferType::Transform);
        prepareCopyCommandsForBuffer(BufferType::Material);
        prepareCopyCommandsForBuffer(BufferType::Draw);
        prepareCopyCommandsForBuffer(BufferType::Light);
    }

    void uploadToGPU(const uint32_t frameIndex) {
        for (auto it = m_CopyCommands.begin(); it != m_CopyCommands.end();) {
            uint32_t objectCount = it->end - it->start;
            void * dataSource;
            void * dataDestination;
            size_t layoutSize;

            if (it->frameIndex != frameIndex) {
                ++it;
                continue;
            }

            switch (it->type) {
                case BufferType::Transform:
                    dataSource = m_Transforms.getCpuSideData();
                    dataDestination = m_Transforms.getMappedMemory(frameIndex);
                    layoutSize = m_Transforms.getLayoutSize();
                    break;
                case BufferType::Material:
                    dataSource = m_Materials.getCpuSideData();
                    dataDestination = m_Materials.getMappedMemory(frameIndex);
                    layoutSize = m_Materials.getLayoutSize();
                    break;
                case BufferType::Draw:
                    dataSource = m_Draws.getCpuSideData();
                    dataDestination = m_Draws.getMappedMemory(frameIndex);
                    layoutSize = m_Draws.getLayoutSize();
                    break;
                case BufferType::Light:
                    dataSource = m_Lights.getCpuSideData();
                    dataDestination = m_Lights.getMappedMemory(frameIndex);
                    layoutSize = m_Lights.getLayoutSize();
                    break;
                default:
                    throw std::runtime_error("Unknown buffer layout.");
            }

            dataSource = static_cast<char *>(dataSource) + layoutSize * it->start;
            dataDestination = static_cast<char *>(dataDestination) + layoutSize * it->start;

            std::memcpy(dataDestination, dataSource, layoutSize * objectCount);

            it = m_CopyCommands.erase(it);
        }
    }

    AddressMappings getAddressMappingsForFrame(const uint32_t frameIndex) const {
        AddressMappings result;
        result.transformAddress = m_Transforms.getBufferDeviceAddressForFrame(frameIndex);
        result.materialAddress = m_Materials.getBufferDeviceAddressForFrame(frameIndex);
        result.drawAddress = m_Draws.getBufferDeviceAddressForFrame(frameIndex);
        result.lightAddress = m_Lights.getBufferDeviceAddressForFrame(frameIndex);

        return result;
    }

    uint32_t addTransform() {
        return m_Transforms.add();
    }

    TransformLayout & getTransform(const uint32_t transformIndex) {
        return m_Transforms.getAndMarkDirty(transformIndex);
    }

    void removeTransform(const uint32_t transformIndex) {
        m_Transforms.remove(transformIndex);
    }

    uint32_t addMaterial(const glm::vec4 & materialTint, const uint32_t textureIndex) {
        const uint32_t materialIndex = m_Materials.add();
        MaterialLayout & material = m_Materials.getAndMarkDirty(materialIndex);
        material.materialTint = materialTint;
        material.textureIndex = textureIndex;

        return materialIndex;
    }

    MaterialLayout & getMaterial(const uint32_t materialIndex) {
        return m_Materials.getAndMarkDirty(materialIndex);
    }

    void removeMaterial(const uint32_t materialIndex) {
        m_Materials.remove(materialIndex);
    }

    uint32_t addDraw() {
        const uint32_t drawIndex = m_Draws.add();
        DrawLayout & draw = m_Draws.getAndMarkDirty(drawIndex);
        draw.enabled = true;

        return drawIndex;
    }

    DrawLayout & getDraw(const uint32_t drawIndex) {
        return m_Draws.getAndMarkDirty(drawIndex);
    }

    void removeDraw(const uint32_t drawIndex) {
        DrawLayout & draw = m_Draws.getAndMarkDirty(drawIndex);
        draw.enabled = false;
        m_Draws.remove(drawIndex);
    }

    uint32_t addLight() {
        return m_Lights.add();
    }

    LightLayout & getLight(const uint32_t lightIndex) {
        return m_Lights.getAndMarkDirty(lightIndex);
    }

    void removeLight(const uint32_t lightIndex) {
        LightLayout & light = m_Lights.getAndMarkDirty(lightIndex);

        light.enabled = false;
        light.transformIndex = INDEX_UNSET;

        m_Lights.remove(lightIndex);
    }

    uint32_t getLightCount() const {
        return m_Lights.getObjectCount();
    }

private:
    void prepareCopyCommandsForBuffer(const BufferType type) {
        std::vector<bool> * dirtyFlags;
        switch (type) {
            case BufferType::Transform:
                dirtyFlags = &m_Transforms.getDirtyStates();
                break;
            case BufferType::Material:
                dirtyFlags = &m_Materials.getDirtyStates();
                break;
            case BufferType::Draw:
                dirtyFlags = &m_Draws.getDirtyStates();
                break;
            case BufferType::Light:
                dirtyFlags = &m_Lights.getDirtyStates();
                break;
            default:
                throw std::runtime_error("Unknown buffer layout.");
        }

        uint32_t startIndex = INDEX_UNSET;

        for (uint32_t i = 0; i < dirtyFlags->size(); ++i) {
            if (dirtyFlags->at(i) == true) {
                if (startIndex == INDEX_UNSET) {
                    startIndex = i;
                }
                dirtyFlags->at(i) = false;
            } else {
                if (startIndex != INDEX_UNSET) {
                    const uint32_t endIndex = i;

                    for (uint32_t frame = 0; frame < m_MaxFramesInFlight; ++frame) {
                        m_CopyCommands.emplace_back(CopyCommand { type, startIndex, endIndex, frame });
                    }

                    startIndex = INDEX_UNSET;
                }
            }
        }

        // Special case - last element is dirty
        if (startIndex != INDEX_UNSET) {
            const uint32_t endIndex = dirtyFlags->size();

            for (uint32_t frame = 0; frame < m_MaxFramesInFlight; ++frame) {
                m_CopyCommands.emplace_back(CopyCommand { type, startIndex, endIndex, frame });
            }
        }
    }

private:
    uint32_t m_MaxFramesInFlight = 0;

    DeviceBuffer<TransformLayout> m_Transforms {};
    DeviceBuffer<MaterialLayout> m_Materials {};
    DeviceBuffer<DrawLayout> m_Draws {};
    DeviceBuffer<LightLayout> m_Lights {};

    uint32_t m_CopiedFrames = 0;
    std::vector<CopyCommand> m_CopyCommands {};
};
