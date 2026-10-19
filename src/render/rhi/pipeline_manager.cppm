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

import constants;
import descriptor_manager;
import device_mapper;
import hlsl_compiler;
import render_types;
import scene_graph_types;
import vulkan_instance;
import vulkan_resource_service;

import glm;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

#ifndef DISABLE_IMPORT_STD
import std;
#endif

struct Pipeline {
    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::PipelineLayout layout = nullptr;

    vk::PipelineBindPoint bindPoint {};

    vk::DescriptorPool descriptorPool = nullptr;
    vk::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::DescriptorSet descriptorSet = nullptr;
};

export class PipelineManager;
GENERATE_LOCATOR(PipelineManager)

class PipelineManager {
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

    void createComputePipeline() {
        auto [csShaderModule, csShaderStageInfo] = m_ShaderCompiler.compileWithInfo(
            m_Instance->getRaiiDevice(),
            "shaders/particle_compute_shader.hlsl",
            HlslShaderStage::COMPUTE
        );

        vk::PushConstantRange pushConstantRange = {
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .size = sizeof(ComputePushConstants)
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {
            .setLayoutCount = 0,
            .pSetLayouts = nullptr,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange
        };

        auto computePipelineLayout = vk::raii::PipelineLayout(m_Instance->getRaiiDevice(), pipelineLayoutInfo);

        vk::ComputePipelineCreateInfo pipelineCreateInfo = {
            .stage = csShaderStageInfo,
            .layout = computePipelineLayout
        };

        auto computePipeline = m_Instance->getRaiiDevice().createComputePipeline(nullptr, pipelineCreateInfo);

        Pipeline newPipeline {
            .pipeline = std::move(computePipeline),
            .layout = std::move(computePipelineLayout),
            .bindPoint = vk::PipelineBindPoint::eCompute,
        };

        m_PipelineMap.insert({COMPUTE_PIPELINE_NAME, std::move(newPipeline)});
    }

    void createGraphicsPipeline(vk::SampleCountFlagBits msaaSamples, vk::SurfaceFormatKHR swapChainSurfaceFormat) {
        auto [vsShaderModule, vsShaderStageInfo] = m_ShaderCompiler.compileWithInfo(
            m_Instance->getRaiiDevice(),
            "shaders/main_shader.hlsl",
            HlslShaderStage::VERTEX
        );
        auto [psShaderModule, psShaderStageInfo] = m_ShaderCompiler.compileWithInfo(
            m_Instance->getRaiiDevice(),
            "shaders/main_shader.hlsl",
            HlslShaderStage::PIXEL
        );

        auto bindingDescription = SceneGraphTypes::Attribute::getBindingDescription();
        auto attributeDescriptions = SceneGraphTypes::Attribute::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data()
        };
        vk::PipelineShaderStageCreateInfo shaderStages[] = { vsShaderStageInfo, psShaderStageInfo };

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly { .topology = vk::PrimitiveTopology::eTriangleList };

        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState {
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };
        // don't need to specify viewports and scissors now as we are going to be setting them dynamically
        vk::PipelineViewportStateCreateInfo viewportState { .viewportCount = 1, .scissorCount = 1 };

        vk::PipelineRasterizationStateCreateInfo rasterizer {
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise, // counter-clockwise since we flip the Y-axis of the projection matrix
            .depthBiasEnable = vk::False,
            .lineWidth = 1.0f
        };

        vk::PipelineMultisampleStateCreateInfo multisampling {
            .rasterizationSamples = msaaSamples,
            .sampleShadingEnable = vk::False
        };

        vk::PipelineDepthStencilStateCreateInfo depthStencil {
            .depthTestEnable = vk::True,
            .depthWriteEnable = vk::True,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable = vk::False
        };

        vk::PipelineColorBlendAttachmentState colorBlendAttachment {
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };

        vk::PipelineColorBlendStateCreateInfo colorBlending {
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        vk::PushConstantRange pushConstantRange = {
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
            .size = PipelineManager::getVertexPushConstantsSize()
        };

        auto * descriptorManager = DescriptorManagerLocator::locate();

        vk::DescriptorSetLayout descriptorSetLayout = descriptorManager->getDescriptorSetLayout();
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
            .setLayoutCount = 1,                                    // # of descriptor set layouts
            .pSetLayouts = &descriptorSetLayout,                 // Pointer to descriptor set layouts
            .pushConstantRangeCount = 1,                            // # of push constant ranges
            .pPushConstantRanges = &pushConstantRange               // Pointer to push constant ranges
        };
        auto graphicsPipelineLayout = vk::raii::PipelineLayout(m_Instance->getRaiiDevice(), pipelineLayoutInfo);

        auto * vulkanResourceService = VulkanResourceServiceLocator::locate();

        vk::Format depthFormat = vulkanResourceService->findDepthFormat();
        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
            .depthAttachmentFormat = depthFormat
        };
        vk::GraphicsPipelineCreateInfo pipelineInfo {
            .pNext = &pipelineRenderingCreateInfo,
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = graphicsPipelineLayout,
            .renderPass = nullptr                   // we're using dynamic rendering instead of render passes
        };

        auto graphicsPipeline = vk::raii::Pipeline(m_Instance->getRaiiDevice(), nullptr, pipelineInfo);

        Pipeline newPipeline {
            .pipeline = std::move(graphicsPipeline),
            .layout = std::move(graphicsPipelineLayout),
            .bindPoint = vk::PipelineBindPoint::eGraphics,
            .descriptorPool = descriptorManager->getDescriptorPool(),
            .descriptorSetLayout = descriptorManager->getDescriptorSetLayout(),
            .descriptorSet = descriptorManager->getTextureDescriptorSet()
        };

        m_PipelineMap.insert({GRAPHICS_PIPELINE_NAME, std::move(newPipeline)});
    }

    void createPointGraphicsPipeline(vk::SampleCountFlagBits msaaSamples, vk::SurfaceFormatKHR swapChainSurfaceFormat) {
        auto [vsShaderModule, vsShaderStageInfo] = m_ShaderCompiler.compileWithInfo(
            m_Instance->getRaiiDevice(),
            "shaders/particle_graphics_shader.hlsl",
            HlslShaderStage::VERTEX
        );
        auto [psShaderModule, psShaderStageInfo] = m_ShaderCompiler.compileWithInfo(
            m_Instance->getRaiiDevice(),
            "shaders/particle_graphics_shader.hlsl",
            HlslShaderStage::PIXEL
        );

        auto bindingDescription = Particle::getBindingDescription();
        auto attributeDescriptions = Particle::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data()
        };

        vk::PipelineShaderStageCreateInfo shaderStages[] = { vsShaderStageInfo, psShaderStageInfo };

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly { .topology = vk::PrimitiveTopology::ePointList };

        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState {
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };
        vk::PipelineViewportStateCreateInfo viewportState { .viewportCount = 1, .scissorCount = 1 };

        vk::PipelineRasterizationStateCreateInfo rasterizer {
            .depthClampEnable = vk::False,
            .rasterizerDiscardEnable = vk::False,
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .depthBiasEnable = vk::False,
            .lineWidth = 1.0f
        };

        vk::PipelineMultisampleStateCreateInfo multisampling {
            .rasterizationSamples = msaaSamples,
            .sampleShadingEnable = vk::False
        };

        vk::PipelineDepthStencilStateCreateInfo depthStencil {
            .depthTestEnable = vk::False,
            .depthWriteEnable = vk::True,
            .depthCompareOp = vk::CompareOp::eLess,
            .depthBoundsTestEnable = vk::False,
            .stencilTestEnable = vk::False
        };

        vk::PipelineColorBlendAttachmentState colorBlendAttachment {
            .blendEnable = vk::True,
            .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };

        vk::PipelineColorBlendStateCreateInfo colorBlending {
            .logicOpEnable = vk::False,
            .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        vk::PushConstantRange pushConstantRange = {
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
            .size = PipelineManager::getVertexPushConstantsSize()
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
            .setLayoutCount = 0,                                    // # of descriptor set layouts
            .pSetLayouts = nullptr,                                 // Pointer to descriptor set layouts
            .pushConstantRangeCount = 1,                            // # of push constant ranges
            .pPushConstantRanges = &pushConstantRange               // Pointer to push constant ranges
        };
        auto pointGraphicsPipelineLayout = vk::raii::PipelineLayout(m_Instance->getRaiiDevice(), pipelineLayoutInfo);

        auto * vulkanResourceService = VulkanResourceServiceLocator::locate();

        vk::Format depthFormat = vulkanResourceService->findDepthFormat();
        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
            .depthAttachmentFormat = depthFormat
        };

        vk::GraphicsPipelineCreateInfo pipelineInfo {
            .pNext = &pipelineRenderingCreateInfo,
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = pointGraphicsPipelineLayout,
            .renderPass = nullptr                   // we're using dynamic rendering instead of render passes
        };

        auto pointGraphicsPipeline = vk::raii::Pipeline(m_Instance->getRaiiDevice(), nullptr, pipelineInfo);

        Pipeline newPipeline {
            .pipeline = std::move(pointGraphicsPipeline),
            .layout = std::move(pointGraphicsPipelineLayout),
            .bindPoint = vk::PipelineBindPoint::eGraphics,
        };

        m_PipelineMap.insert({POINT_GRAPHICS_PIPELINE_NAME, std::move(newPipeline)});
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

    void bindVertexPushConstants() const {
        m_ActiveCommandBuffer.pushConstants(
            m_ActivePipeline->layout,
            vk::ShaderStageFlagBits::eVertex,
            0,
            sizeof(VertexPushConstants),
            &m_VertexPushConstants
        );
    }

    void bindComputePushConstants() const {
        m_ActiveCommandBuffer.pushConstants(
            m_ActivePipeline->layout,
            vk::ShaderStageFlagBits::eCompute,
            0,
            sizeof(ComputePushConstants),
            &m_ComputePushConstants
        );
    }

    void bindAndDraw(const vk::Buffer vertexBuffer, uint32_t vertexCount, const uint32_t instanceCount) const {
        bindVertexPushConstants();

        m_ActiveCommandBuffer.bindVertexBuffers(0, vertexBuffer, { 0 });
        m_ActiveCommandBuffer.draw(vertexCount, instanceCount, 0, 0);
    }

    void bindAndDrawIndexed(const uint32_t drawIndex, const vk::Buffer vertexBuffer, const vk::Buffer indexBuffer, const uint32_t indexCount, const uint32_t instanceCount) {
        m_VertexPushConstants.drawIndex = drawIndex;
        bindVertexPushConstants();

        m_ActiveCommandBuffer.bindVertexBuffers(0, vertexBuffer, { 0 });
        m_ActiveCommandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
        m_ActiveCommandBuffer.drawIndexed(indexCount, instanceCount, 0, 0, 0);
    }

    // TODO add instancing support
    void executeDrawCall(SceneGraphTypes::DrawCallData & drawData) {
        m_VertexPushConstants.drawIndex = drawData.drawIndex;
        bindVertexPushConstants();

        m_ActiveCommandBuffer.bindVertexBuffers(0, drawData.vertexBuffer, { drawData.vertexOffsetBytes });
        m_ActiveCommandBuffer.bindIndexBuffer(drawData.indexBuffer, 0, vk::IndexType::eUint32);
        m_ActiveCommandBuffer.drawIndexed(drawData.indexCount, 1, drawData.firstIndex, 0, 0);
    }

    void destroyAll() {
        m_ActivePipeline = nullptr;
        m_ActiveCommandBuffer = nullptr;
        m_PipelineMap.clear();
    }

private:
    VulkanInstance * m_Instance;

    HlslShaderCompiler m_ShaderCompiler {};

    std::unordered_map<std::string, Pipeline> m_PipelineMap;
    Pipeline * m_ActivePipeline;
    vk::CommandBuffer m_ActiveCommandBuffer;

    VertexPushConstants m_VertexPushConstants;
    ComputePushConstants m_ComputePushConstants;
};
