module;

#include <cassert>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#endif

#include "core/util_macros.h"

export module vulkan_resource_service;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

import platform;
import vulkan_instance;

using std::uint32_t;
using std::int32_t;
using std::memcpy;

export struct VulkanImageData {
    vk::Image image = nullptr;
    vk::DeviceMemory imageMemory = nullptr;
    vk::ImageView imageView = nullptr;
    vk::Sampler sampler = nullptr;
    vk::DeviceSize offset = 0;
    uint32_t mipLevels = 0;
};

// TODO replace with using a single buffer + VMA instead eventually
export struct VulkanBufferData {
    vk::Buffer buffer = nullptr;
    vk::DeviceMemory bufferMemory = nullptr;
};

export struct VulkanShaderBufferData {
    vk::Buffer buffer = nullptr;
    vk::DeviceMemory bufferMemory = nullptr;
    void * mappedMemory = nullptr;
    vk::DeviceAddress bufferDeviceAddress = 0;          // offset this to access/modify data in the shader
};

// Doesn't have mapped memory
export struct VulkanComputeBufferData {
    vk::Buffer buffer = nullptr;
    vk::DeviceMemory bufferMemory = nullptr;
    vk::DeviceAddress bufferDeviceAddress = 0;
};

export class VulkanResourceService;
GENERATE_LOCATOR(VulkanResourceService)

// "Backbone" of the renderer, holds functions related to memory/resource allocation
// as well as the Vulkan handles related to their use; largely self-contained
//
// All handles returned by the public functions of this service will need to be manually freed later on by the caller
class VulkanResourceService {
public:
    VulkanResourceService() {}

    VulkanImageData createCombinedImageSamplerTexture(const StbImageWrapper & textureImage) const {
        VulkanImageData imageData = createSampledImageTexture(textureImage);
        imageData.sampler = createTextureSampler();

        return imageData;
    }

    VulkanImageData createSampledImageTexture(const StbImageWrapper & textureImage) const {
        VulkanImageData imageData {};

        createTextureImage(textureImage, imageData);
        // TODO choose VkFormat conditionally
        imageData.imageView = createImageView(
            imageData.image,
            vk::Format::eR8G8B8A8Srgb,
            vk::ImageAspectFlagBits::eColor,
            imageData.mipLevels
        );

        return imageData;
    }

    vk::Sampler createTextureSampler() const {
        vk::PhysicalDeviceProperties properties = m_Instance->getPhysicalDeviceProperties();
        // https://docs.vulkan.org/tutorial/latest/06_Texture_mapping/01_Image_view_and_sampler.html#_samplers
        // for details on what parameters do what ^^
        vk::SamplerCreateInfo samplerInfo = {
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .mipLodBias = 0.0f,
            .anisotropyEnable = vk::True,
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = vk::False,
            .compareOp = vk::CompareOp::eAlways,
            .minLod = 0.0f,
            .maxLod = vk::LodClampNone,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False
        };

        return m_Instance->getDevice().createSampler(samplerInfo);
    }

    template <typename T>
    VulkanBufferData createVulkanBuffer(vk::DeviceSize bufferSize, vk::BufferUsageFlags bufferTypeFlags, const T * data) {
        VulkanBufferData bufferData {};

        vk::Buffer stagingBuffer {};
        vk::DeviceMemory stagingBufferMemory {};

        createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            stagingBuffer,
            stagingBufferMemory
        );

        void * stagingData = m_Instance->getDevice().mapMemory(stagingBufferMemory, 0, bufferSize);
        memcpy(stagingData, data, bufferSize);
        m_Instance->getDevice().unmapMemory(stagingBufferMemory);

        createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eTransferDst | bufferTypeFlags,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            bufferData.buffer,
            bufferData.bufferMemory
        );
        copyBuffer(stagingBuffer, bufferData.buffer, bufferSize);

        m_Instance->getDevice().freeMemory(stagingBufferMemory);
        m_Instance->getDevice().destroyBuffer(stagingBuffer);

        return bufferData;
    }

    template<typename T>
    VulkanShaderBufferData createShaderBuffer(const uint32_t objectCount) const {
        return createShaderBuffer(objectCount * sizeof(T));
    }

    VulkanShaderBufferData createShaderBuffer(const vk::DeviceSize bufferSize) const {
        vk::Buffer buffer;
        vk::DeviceMemory bufferMemory;
        createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            buffer,
            bufferMemory
        );
        void * mappedMemory = m_Instance->getDevice().mapMemory(bufferMemory, 0, bufferSize);

        vk::BufferDeviceAddressInfo deviceAddressInfo = {
            .buffer = buffer
        };
        vk::DeviceAddress bufferDeviceAddress = m_Instance->getDevice().getBufferAddress(deviceAddressInfo);

        return VulkanShaderBufferData {
            buffer,
            bufferMemory,
            mappedMemory,
            bufferDeviceAddress
        };
    }

    template <typename T>
    std::vector<VulkanComputeBufferData> createComputeBuffers(int maxFramesInFlight, std::vector<T> & data) {
        assert(!data.empty());

        std::vector<VulkanComputeBufferData> computeBufferData {};

        vk::Buffer stagingBuffer {};
        vk::DeviceMemory stagingBufferMemory {};

        vk::DeviceSize bufferSize = sizeof(data[0]) * data.size();
        createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            stagingBuffer,
            stagingBufferMemory
        );

        void * stagingData = m_Instance->getDevice().mapMemory(stagingBufferMemory, 0, bufferSize);
        memcpy(stagingData, data.data(), bufferSize);
        m_Instance->getDevice().unmapMemory(stagingBufferMemory);

        for (size_t i = 0; i < maxFramesInFlight; ++i) {
            vk::Buffer computeBuffer;
            vk::DeviceMemory computeBufferMemory;
            createBuffer(
                bufferSize,
                vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eVertexBuffer |
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                computeBuffer,
                computeBufferMemory
            );
            copyBuffer(stagingBuffer, computeBuffer, bufferSize);

            vk::BufferDeviceAddressInfo deviceAddressInfo = {
                .buffer = computeBuffer
            };
            vk::DeviceAddress bufferDeviceAddress = m_Instance->getDevice().getBufferAddress(deviceAddressInfo);

            computeBufferData.emplace_back(
                VulkanComputeBufferData {
                    .buffer = computeBuffer,
                    .bufferMemory = computeBufferMemory,
                    .bufferDeviceAddress = bufferDeviceAddress
                }
            );
        }

        m_Instance->getDevice().freeMemory(stagingBufferMemory);
        m_Instance->getDevice().destroyBuffer(stagingBuffer);

        return computeBufferData;
    }

    VulkanImageData createGenericResources(
        const vk::Format format,
        const vk::Extent2D extent,
        const vk::SampleCountFlagBits msaaSamples,
        const vk::ImageUsageFlags usageFlags,
        vk::MemoryPropertyFlags memoryProperties,
        const uint32_t mipLevels,
        vk::ImageAspectFlags aspectFlags
    ) const {
        VulkanImageData resources {};
        resources.mipLevels = mipLevels;

        createImage(
            extent.width,
            extent.height,
            mipLevels,
            msaaSamples,
            format,
            vk::ImageTiling::eOptimal,
            usageFlags,
            memoryProperties,
            resources.image,
            resources.imageMemory
        );
        resources.imageView = createImageView(
            resources.image,
            format,
            aspectFlags,
            mipLevels
        );

        return resources;
    }

    VulkanImageData createColorResources(const vk::Format colorFormat, const vk::Extent2D swapChainExtent, const vk::SampleCountFlagBits msaaSamples) const {
        return createGenericResources(
            colorFormat,
            swapChainExtent,
            msaaSamples,
            vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            1,
            vk::ImageAspectFlagBits::eColor
        );
    }

    VulkanImageData createDepthResources(const vk::Format depthFormat, const vk::Extent2D swapChainExtent, const vk::SampleCountFlagBits msaaSamples) const {
        return createGenericResources(
            depthFormat,
            swapChainExtent,
            msaaSamples,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            1,
            vk::ImageAspectFlagBits::eDepth
        );
    }

    void freeResources(vk::Image & image, vk::DeviceMemory & deviceMemory, vk::ImageView & imageView, vk::Sampler & sampler) const {
        if (sampler != nullptr) {
            freeResources(sampler);
        }

        freeResources(image, deviceMemory, imageView);
    }

    void freeResources(vk::Image & image, vk::DeviceMemory & deviceMemory, vk::ImageView & imageView) const {
        m_Instance->getDevice().destroyImageView(imageView);
        m_Instance->getDevice().destroyImage(image);
        m_Instance->getDevice().freeMemory(deviceMemory);

        image = nullptr;
        deviceMemory = nullptr;
        imageView = nullptr;
    }

    void freeResources(vk::Sampler & sampler) const {
        m_Instance->getDevice().destroySampler(sampler);
        sampler = nullptr;
    }

    void freeResources(vk::ImageView & imageView) const {
        m_Instance->getDevice().destroyImageView(imageView);
        imageView = nullptr;
    }

    void freeResources(VulkanBufferData bufferData) const {
        freeResources(bufferData.buffer, bufferData.bufferMemory);
    }

    void freeResources(vk::Buffer & buffer, vk::DeviceMemory & bufferMemory) const {
        m_Instance->getDevice().freeMemory(bufferMemory);
        m_Instance->getDevice().destroyBuffer(buffer);

        buffer = nullptr;
        bufferMemory = nullptr;
    }

    void freeResourcesAndUnmapMemory(vk::Buffer & buffer, vk::DeviceMemory & bufferMemory) const {
        m_Instance->getDevice().unmapMemory(bufferMemory);
        m_Instance->getDevice().freeMemory(bufferMemory);
        m_Instance->getDevice().destroyBuffer(buffer);

        buffer = nullptr;
        bufferMemory = nullptr;
    }

    vk::Format findDepthFormat() const {
        return findSupportedFormat(
            { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
            vk::ImageTiling::eOptimal,
            vk::FormatFeatureFlagBits::eDepthStencilAttachment
        );
    }

    static void transitionImageLayout(
        const vk::CommandBuffer & commandBuffer,
        const vk::Image & image,
        const vk::ImageLayout oldLayout,
        const vk::ImageLayout newLayout,
        const uint32_t mipLevels = 1
    ) {
        vk::ImageMemoryBarrier2 barrier = {
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1 }
        };

        // Undefined-to-transfer: common for preparing images for data uploads
        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;

            barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;

            // Transfer-to-shader: prepares uploaded images for shader sampling
        } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;

            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;

            // Undefined-to-color: for transitioning swapchain and multisampled images
        } else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
            barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;

            barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;

            // Undefined-to-depth: for transitioning depth image
        } else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthAttachmentOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;

            barrier.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            // Also change the aspect mask to depth instead of color
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;

            // Color-attachment-to-present: for resetting swapchain image after rendering it to the screen
        } else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal && newLayout == vk::ImageLayout::ePresentSrcKHR) {
            barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead;

            barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        } else {
            // Will add new layout combinations as needed
            throw std::invalid_argument("Unsupported layout transition!");
        }

        vk::DependencyInfo dependencyInfo = {
            .dependencyFlags = vk::DependencyFlagBits::eByRegion,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    [[nodiscard]] vk::ShaderModule createShaderModule(const std::vector<char> & code) const {
        vk::ShaderModuleCreateInfo createInfo = {
            .codeSize = code.size() * sizeof(char),
            .pCode = reinterpret_cast<const uint32_t *>(code.data())
        };

        vk::ShaderModule shaderModule = m_Instance->getDevice().createShaderModule(createInfo);
        return shaderModule;
    }

    // This function manually allocates image view memory, meaning it will need to be manually deallocated later
    [[nodiscard]]
    vk::ImageView createImageView(
        const vk::Image & image, const vk::Format format,
        vk::ImageAspectFlags aspectFlags, uint32_t mipLevels
    ) const {
        vk::ImageViewCreateInfo viewInfo = {
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = { aspectFlags, 0, mipLevels, 0, 1 }
        };
        return m_Instance->getDevice().createImageView(viewInfo);
    }

    void copyBuffer(const vk::Buffer & srcBuffer, const vk::Buffer & dstBuffer, const vk::DeviceSize size) const {
        auto commandCopyBuffer = beginSingleTimeCommands();
        commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, vk::BufferCopy(0, 0, size));
        endSingleTimeCommands(commandCopyBuffer);
    }

    void setVulkanInstance(VulkanInstance * instance) {
        m_Instance = instance;
    }

    VulkanInstance & getVulkanInstance() const {
        return *m_Instance;
    }

private:
    void transitionImageLayout(
        const vk::Image & image,
        const vk::ImageLayout oldLayout,
        const vk::ImageLayout newLayout,
        const uint32_t mipLevels = 1
    ) const {
        vk::CommandBuffer commandBuffer = beginSingleTimeCommands();
        transitionImageLayout(commandBuffer, image, oldLayout, newLayout, mipLevels);
        endSingleTimeCommands(commandBuffer);
    }

    void createTextureImage(const StbImageWrapper & textureImage, VulkanImageData & dstImageData) const {
        // multiplying by 4 instead of # of channels because we create image with the format of RGBA
        vk::DeviceSize imageSize = textureImage.width * textureImage.height * 4;

        if (!textureImage.pixels) {
            throw std::runtime_error("failed to load texture image!");
        }

        // Don't need to stage if we are working with a device with a memory type that's both host-visible and device-local
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;
        createBuffer(
            imageSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            stagingBuffer,
            stagingBufferMemory
        );

        void * data = m_Instance->getDevice().mapMemory(stagingBufferMemory, 0, imageSize);
        memcpy(data, textureImage.pixels, imageSize);
        m_Instance->getDevice().unmapMemory(stagingBufferMemory);

        dstImageData.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(textureImage.width, textureImage.height)))) + 1;

        createImage(
            textureImage.width,
            textureImage.height,
            dstImageData.mipLevels,
            vk::SampleCountFlagBits::e1,
            vk::Format::eR8G8B8A8Srgb,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            dstImageData.image,
            dstImageData.imageMemory
        );

        transitionImageLayout(
            dstImageData.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            dstImageData.mipLevels
        );
        copyBufferToImage(
            stagingBuffer,
            dstImageData.image,
            static_cast<uint32_t>(textureImage.width),
            static_cast<uint32_t>(textureImage.height)
        );
        // Transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps
        generateMipmaps(
            dstImageData.image,
            vk::Format::eR8G8B8A8Srgb,
            textureImage.width,
            textureImage.height,
            dstImageData.mipLevels
        );

        // Cleanup
        m_Instance->getDevice().freeMemory(stagingBufferMemory);
        m_Instance->getDevice().destroyBuffer(stagingBuffer);
    }

    // This function manually allocates image memory, meaning it will need to be manually deallocated later
    void createImage(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        vk::SampleCountFlagBits numSamples,
        vk::Format format,
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties,
        vk::Image & image,
        vk::DeviceMemory & deviceMemory
    ) const {
        vk::ImageCreateInfo imageInfo = {
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = { width, height, 1 },
            .mipLevels = mipLevels,
            .arrayLayers = 1,
            .samples = numSamples,
            .tiling = tiling,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };
        image = m_Instance->getDevice().createImage(imageInfo);

        vk::MemoryRequirements memRequirements = m_Instance->getDevice().getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo = {
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
        };
        deviceMemory = m_Instance->getDevice().allocateMemory(allocInfo);
        m_Instance->getDevice().bindImageMemory(image, deviceMemory, 0);
    }

    vk::CommandBuffer beginSingleTimeCommands() const {
        vk::CommandBufferAllocateInfo allocInfo = {
            .commandPool = m_Instance->getCommandPool(),
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };
        vk::CommandBuffer commandBuffer = std::move(m_Instance->getDevice().allocateCommandBuffers(allocInfo).front());

        vk::CommandBufferBeginInfo beginInfo { .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
        commandBuffer.begin(beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(vk::CommandBuffer & commandBuffer) const {
        commandBuffer.end();

        vk::SubmitInfo submitInfo { .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };
        m_Instance->getGraphicsQueue().submit(submitInfo, nullptr);
        // using a fence instead of waitIdle() would allow us to schedule multiple transfer simultaneously and
        // wait for all of them to complete instead of executing one at a time. ( = likely better optimization)
        m_Instance->getGraphicsQueue().waitIdle();

        m_Instance->getDevice().freeCommandBuffers(m_Instance->getCommandPool(), 1, &commandBuffer);
    }

    // TODO parametrize offset/don't create different buffers for every texture
    void createBuffer(
        vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties,
        vk::Buffer & buffer, vk::DeviceMemory & bufferMemory
    ) const {
        vk::BufferCreateInfo bufferInfo = {
            .size = size,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };
        buffer = m_Instance->getDevice().createBuffer(bufferInfo);
        vk::MemoryRequirements memRequirements = m_Instance->getDevice().getBufferMemoryRequirements(buffer);
        vk::MemoryAllocateInfo allocInfo = {
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
        };

        if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
            vk::MemoryAllocateFlagsInfo allocFlagsInfo = {
                .flags = vk::MemoryAllocateFlagBits::eDeviceAddress
            };
            allocInfo.pNext = &allocFlagsInfo;
        }

        bufferMemory = m_Instance->getDevice().allocateMemory(allocInfo);
        m_Instance->getDevice().bindBufferMemory(buffer, bufferMemory, 0);
    }

    uint32_t findMemoryType(const uint32_t typeFilter, const vk::MemoryPropertyFlags properties) const {
        vk::PhysicalDeviceMemoryProperties memProperties = m_Instance->getPhysicalDevice().getMemoryProperties();
        // only concerning ourselves about memory types for now, not the heaps that memory comes from
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type!");
    }

    void copyBufferToImage(const vk::Buffer & buffer, const vk::Image & image, const uint32_t width, const uint32_t height) const {
        vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

        // TODO parametrize offsets
        vk::BufferImageCopy region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { width, height, 1 }
        };
        commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, { region });
        // Submit the buffer copy to the graphics queue
        endSingleTimeCommands(commandBuffer);
    }

    // TODO: Try implementing software resizing and/or loading multiple mip levels from a single file
    void generateMipmaps(const vk::Image & image, const vk::Format imageFormat, const int32_t texWidth, const int32_t texHeight, const uint32_t mipLevels) const {
        // Check if image format supports linear blit-ing
        vk::FormatProperties formatProperties = m_Instance->getPhysicalDevice().getFormatProperties(imageFormat);
        if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
            throw std::runtime_error("Texture image format does not support linear blitting!");
        }

        vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

        vk::ImageMemoryBarrier barrier = {
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image
        };
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;

        for (uint32_t i = 1; i < mipLevels; ++i) {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                {}, {}, {}, barrier
            );

            vk::ArrayWrapper1D<vk::Offset3D, 2> srcOffsets, dstOffsets;
            srcOffsets[0] = vk::Offset3D(0, 0, 0);
            srcOffsets[1] = vk::Offset3D(mipWidth, mipHeight, 1);
            dstOffsets[0] = vk::Offset3D(0, 0, 0);
            dstOffsets[1] = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1);
            vk::ImageBlit blit = {
                .srcSubresource = {},
                .srcOffsets = srcOffsets,
                .dstSubresource = {},
                .dstOffsets = dstOffsets
            };
            blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, 1);
            blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);
            commandBuffer.blitImage(
                image,
                vk::ImageLayout::eTransferSrcOptimal,
                image,
                vk::ImageLayout::eTransferDstOptimal,
                { blit },
                vk::Filter::eLinear
            );

            barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier
            );

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        // For the last mip level, we go straight from dstOptimal to shaderReadOnlyOptimal, since we don't need to blit from it
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, barrier
        );

        endSingleTimeCommands(commandBuffer);
    }

    vk::Format findSupportedFormat(const std::vector<vk::Format> & candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const {
        for (const auto format : candidates) {
            vk::FormatProperties props = m_Instance->getPhysicalDevice().getFormatProperties(format);
            if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
                return format;
            }
            if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        throw std::runtime_error("Failed to find supported format!");
    }

private:
    VulkanInstance * m_Instance = nullptr;
};
