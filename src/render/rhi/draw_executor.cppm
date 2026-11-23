module;

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

#include <cstdint>

#include "core/util_macros.h"

export module draw_executor;

import device_mapper;
import pipeline_manager;
import scene_graph_types;
import vulkan_resource_service;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

#ifndef DISABLE_IMPORT_STD
import std;
#endif

struct IndirectBatch {
    vk::Buffer vertexBuffer;
    vk::Buffer indexBuffer;
    uint32_t vertexOffset;
    uint32_t indexCount;
    uint32_t firstIndex;

    uint32_t drawIndex;
    uint32_t transformListIndex;
};

export class DrawExecutor;

GENERATE_LOCATOR(DrawExecutor);

// Just handles indirect draws for now, in the future might move other draw call functionality into here
class DrawExecutor {
public:
    DrawExecutor() = default;

    void initialize() {
        auto * vulkanResourceService = VulkanResourceServiceLocator::locate();

        vk::DeviceSize initialBufferSize = 1000 * sizeof(vk::DrawIndexedIndirectCommand);
        m_GpuBatchBuffer = vulkanResourceService->createDrawIndirectGPUBuffer(initialBufferSize);
        m_CpuBatchBuffer = vulkanResourceService->createDrawIndirectStagingBuffer(initialBufferSize);
    }

    void destroy() {
        auto * vulkanResourceService = VulkanResourceServiceLocator::locate();

        vulkanResourceService->freeResources(m_GpuBatchBuffer.buffer, m_GpuBatchBuffer.bufferMemory);
        vulkanResourceService->freeResources(m_CpuBatchBuffer.buffer, m_CpuBatchBuffer.bufferMemory);
    }

    void prepareForNextFrame() {
        m_Batches.clear();
        for (auto & v : m_InstanceTransforms) {
            v.clear();
        }
        m_DrawCommands.clear();
    }

    void batchDrawCall(SceneGraphTypes::DrawCallData & drawData) {
        for (const auto & batch : m_Batches) {
            if (batch.drawIndex == drawData.drawIndex) {
                m_InstanceTransforms.at(batch.transformListIndex).emplace_back(drawData.instanceTransformIndex);

                return;
            }
        }

        IndirectBatch newBatch {
            .vertexBuffer = drawData.vertexBuffer,
            .indexBuffer = drawData.indexBuffer,
            .vertexOffset = drawData.vertexOffset,
            .indexCount = drawData.indexCount,
            .firstIndex = drawData.firstIndex,
            .drawIndex = drawData.drawIndex,
        };

        uint32_t instanceTransformListIndex = 0;
        while (true) {
            const bool reachedEndOfList = instanceTransformListIndex >= m_InstanceTransforms.size();

            if (reachedEndOfList) {
                m_InstanceTransforms.emplace_back();
                newBatch.transformListIndex = instanceTransformListIndex;
                m_InstanceTransforms.at(newBatch.transformListIndex).emplace_back(drawData.instanceTransformIndex);

                break;
            }

            const bool reachedEmptyList = m_InstanceTransforms.at(instanceTransformListIndex).size() == 0;

            if (reachedEmptyList) {
                newBatch.transformListIndex = instanceTransformListIndex;
                m_InstanceTransforms.at(newBatch.transformListIndex).emplace_back(drawData.instanceTransformIndex);
                break;
            }

            instanceTransformListIndex++;
        }

        m_Batches.emplace_back(newBatch);
    }

    void uploadBatchesToGPU() {
        auto * deviceMapper = DeviceMapperLocator::locate();
        deviceMapper->resetBatchData();

        for (const auto & batch : m_Batches) {
            for (auto i = 0; i < m_InstanceTransforms.at(batch.transformListIndex).size(); ++i) {
                vk::DrawIndexedIndirectCommand cmd {};
                cmd.indexCount = batch.indexCount;
                cmd.instanceCount = 1;
                cmd.firstIndex = batch.firstIndex;
                cmd.vertexOffset = batch.vertexOffset;
                cmd.firstInstance = 0;

                m_DrawCommands.emplace_back(cmd);

                deviceMapper->addInstanceTransformToBatch(m_InstanceTransforms.at(batch.transformListIndex).at(i));
            }
        }

        const vk::DeviceSize requiredBufferSize = m_DrawCommands.size() * sizeof(vk::DrawIndexedIndirectCommand);
        auto * vulkanResourceService = VulkanResourceServiceLocator::locate();

        if (requiredBufferSize > m_GpuBatchBuffer.size) {
            const vk::DeviceSize newBufferSize = requiredBufferSize + 100 * sizeof(vk::DrawIndexedIndirectCommand);
            // TODO test
            vulkanResourceService->freeResources(m_GpuBatchBuffer.buffer, m_GpuBatchBuffer.bufferMemory);
            m_GpuBatchBuffer = vulkanResourceService->createDrawIndirectGPUBuffer(newBufferSize);
        }
        vulkanResourceService->stageAndTransfer(m_CpuBatchBuffer, m_GpuBatchBuffer, m_DrawCommands);
    }

    void executeDrawIndirect(const uint32_t frameIndex) {
        auto * pipelineManager = PipelineManagerLocator::locate();
        auto & commandBuffer = pipelineManager->getActiveCommandBuffer();

        pipelineManager->prepareAddressesForFrame(frameIndex);

        uint32_t batchOffset = 0;
        for (auto & batch : m_Batches) {
            commandBuffer.bindVertexBuffers(0, batch.vertexBuffer, batch.vertexOffset);
            commandBuffer.bindIndexBuffer(batch.indexBuffer, 0, vk::IndexType::eUint32);

            pipelineManager->setVertexDrawIndex(batch.drawIndex);
            pipelineManager->setVertexBatchOffset(batchOffset);
            pipelineManager->bindVertexPushConstants();

            constexpr uint32_t indirectStride = sizeof(vk::DrawIndexedIndirectCommand);
            const uint32_t indirectOffset = batchOffset * sizeof(vk::DrawIndexedIndirectCommand);

            const uint32_t drawCount = static_cast<uint32_t>(m_InstanceTransforms.at(batch.transformListIndex).size());

            commandBuffer.drawIndexedIndirect(m_GpuBatchBuffer.buffer, indirectOffset, drawCount, indirectStride);

            batchOffset += drawCount;
        }
    }

private:
    SizedVulkanBufferData m_GpuBatchBuffer {};
    SizedVulkanBufferData m_CpuBatchBuffer {};
    std::vector<IndirectBatch> m_Batches {};
    std::vector<std::vector<uint32_t>> m_InstanceTransforms {};
    std::vector<vk::DrawIndexedIndirectCommand> m_DrawCommands {};
};
