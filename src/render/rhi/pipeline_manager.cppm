module;

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

#ifdef DISABLE_IMPORT_STD
#include <string>
#include <unordered_map>
#endif

#include <cstdint>

#include "core/util_macros.h"

export module pipeline_manager;

import device_mapper;
import render_types;
import vulkan_instance;

import glm;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

#ifndef DISABLE_IMPORT_STD
import std;
#endif

struct Pipeline {
    vk::PipelineBindPoint bindPoint;
    vk::PipelineLayout layout;
    vk::Pipeline pipeline;

    vk::DescriptorPool descriptorPool = nullptr;
    vk::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::DescriptorSet descriptorSet = nullptr;
};

export class PipelineManager;
GENERATE_LOCATOR(PipelineManager)

export class PipelineManager {
private:
    // Need to be careful with this - push constant size is limited
    // NOTE: Order of variables here has to be the exact same as on GPU side!
    struct VertexPushConstants {
        // Set this before rendering entities
        glm::mat4 projection;
        glm::mat4 view;

        vk::DeviceAddress drawAddress;
        vk::DeviceAddress lightAddress;
        vk::DeviceAddress transformAddress;
        vk::DeviceAddress materialAddress;

        uint32_t particlesEnabled;
        LightMode lightMode;
        uint32_t lightCount;

        // Set this while rendering entities
        uint32_t drawIndex;
    };

    struct ComputePushConstants {
        vk::DeviceAddress addressThisFrame;
        vk::DeviceAddress addressLastFrame;
        float deltaTime;
        uint32_t particlesEnabled;
    };

public:
    PipelineManager() = default;

    void setVulkanInstance(VulkanInstance * instance) {
        m_Instance = instance;
    }

    VertexPushConstants * getVertexPushConstants() {
        return &m_VertexPushConstants;
    }

    static uint32_t getVertexPushConstantsSize() {
        return sizeof(VertexPushConstants);
    }

    void setAndBindActivePipeline(const std::string & pipelineName) {
        m_ActivePipeline = &m_PipelineMap.at(pipelineName);

        m_ActiveCommandBuffer.bindPipeline(m_ActivePipeline->bindPoint, m_ActivePipeline->pipeline);
        if (m_ActivePipeline->descriptorSet != nullptr) {
            m_ActiveCommandBuffer.bindDescriptorSets(
                m_ActivePipeline->bindPoint,
                m_ActivePipeline->layout,
                0,
                m_ActivePipeline->descriptorSet,
                nullptr
            );
        }
    }

    void setActiveCommandBuffer(const vk::CommandBuffer commandBuffer) {
        m_ActiveCommandBuffer = commandBuffer;
    }

    void prepareComputeConstants(const vk::DeviceAddress addressThisFrame, const vk::DeviceAddress addressLastFrame, const float deltaTime) {
        m_ComputePushConstants.addressThisFrame = addressThisFrame;
        m_ComputePushConstants.addressLastFrame = addressLastFrame;
        m_ComputePushConstants.deltaTime = deltaTime;
    }

    void prepareAddressesForFrame(const uint32_t frameIndex) {
        auto addressMappings = DeviceMapperLocator::locate()->getAddressMappingsForFrame(frameIndex);
        m_VertexPushConstants.drawAddress = addressMappings.drawAddress;
        m_VertexPushConstants.lightAddress = addressMappings.lightAddress;
        m_VertexPushConstants.transformAddress = addressMappings.transformAddress;
        m_VertexPushConstants.materialAddress = addressMappings.materialAddress;
    }

    void setLightCount(const uint32_t lightCount) {
        m_VertexPushConstants.lightCount = lightCount;
    }

    void setLightMode(const LightMode lightMode) {
        m_VertexPushConstants.lightMode = lightMode;
    }

    void setProjectionView(const glm::mat4 & projectionMatrix, const glm::mat4 & viewMatrix) {
        m_VertexPushConstants.projection = projectionMatrix;
        // Vulkan's Y coordinate is flipped compared to OpenGL
        // and glm was designed with OpenGL in mind
        m_VertexPushConstants.projection[1][1] *= -1;
        m_VertexPushConstants.view = viewMatrix;
    }

    void setParticlesEnabled(const bool particlesEnabled) {
        m_ComputePushConstants.particlesEnabled = particlesEnabled;
        m_VertexPushConstants.particlesEnabled = particlesEnabled;
    }

    void bindVertices(const vk::Buffer vertexBuffer, const vk::Buffer indexBuffer) const {
        m_ActiveCommandBuffer.bindVertexBuffers(0, vertexBuffer, { 0 });
        m_ActiveCommandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
    }

    void bindVertices(const vk::Buffer vertexBuffer) const {
        m_ActiveCommandBuffer.bindVertexBuffers(0, vertexBuffer, { 0 });
    }

    void bindVertexPushConstants() const {
        m_ActiveCommandBuffer.pushConstants(
            m_ActivePipeline->layout,
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(VertexPushConstants),
            &m_VertexPushConstants
        );
    }

    void bindAndDrawIndexed(const uint32_t drawIndex, const vk::Buffer vertexBuffer, const vk::Buffer indexBuffer, const uint32_t indexCount, const uint32_t instanceCount) {
        m_VertexPushConstants.drawIndex = drawIndex;
        bindVertexPushConstants();

        m_ActiveCommandBuffer.bindVertexBuffers(0, vertexBuffer, { 0 });
        m_ActiveCommandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
        m_ActiveCommandBuffer.drawIndexed(indexCount, instanceCount, 0, 0, 0);
    }

    void registerPipeline(const std::string & pipelineName) {
        m_PipelineMap.insert({ pipelineName, {} });
    }

    void setInfoForPipeline(const std::string & pipelineName, vk::Pipeline vkPipeline, vk::PipelineLayout layout, vk::PipelineBindPoint bindPoint) {
        auto & pipeline = m_PipelineMap.at(pipelineName);
        pipeline.pipeline = vkPipeline;
        pipeline.layout = layout;
        pipeline.bindPoint = bindPoint;
    }

    void setDescriptorInfoForPipeline(const std::string & pipelineName, const vk::DescriptorPool pool, const vk::DescriptorSetLayout layout, const vk::DescriptorSet set) {
        auto & pipeline = m_PipelineMap.at(pipelineName);
        pipeline.descriptorPool = pool;
        pipeline.descriptorSetLayout = layout;
        pipeline.descriptorSet = set;
    }

    void clearAll() {
        m_ActivePipeline = nullptr;
        m_ActiveCommandBuffer = nullptr;
#if 0   // if pipeline manager is actually managing pipeline memory (not the case right now)
        for (auto & p : m_PipelineMap) {
            // TODO make this more modular - probably will need to rearchitect descriptor management at some point
            // can't be bothered for now, got other priorities
            if (p.second.descriptorSet != nullptr) {
                auto res = m_Instance->getDevice().freeDescriptorSets(p.second.descriptorPool, 1, &p.second.descriptorSet);
                if (res != vk::Result::eSuccess) {
                    throw std::runtime_error("Freeing descriptor sets failed for some reason.");
                }
                p.second.descriptorSet = nullptr;
            }
            if (p.second.descriptorSetLayout != nullptr) {
                m_Instance->getDevice().destroyDescriptorSetLayout(p.second.descriptorSetLayout);
                p.second.descriptorSetLayout = nullptr;
            }
            if (p.second.descriptorPool != nullptr) {
                m_Instance->getDevice().destroyDescriptorPool(p.second.descriptorPool);
                p.second.descriptorPool = nullptr;
            }

            m_Instance->getDevice().destroyPipelineLayout(p.second.layout);
            m_Instance->getDevice().destroyPipeline(p.second.pipeline);
        }
#endif
        m_PipelineMap.clear();
    }

private:
    VulkanInstance * m_Instance;

    std::unordered_map<std::string, Pipeline> m_PipelineMap;
    Pipeline * m_ActivePipeline;
    vk::CommandBuffer m_ActiveCommandBuffer;

    VertexPushConstants m_VertexPushConstants;
    ComputePushConstants m_ComputePushConstants;
};
