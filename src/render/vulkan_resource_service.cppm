module;

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

export module vulkan_resource_service;

import std.compat;
import platform;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

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

export struct VulkanUniformBufferData {
    vk::Buffer buffer = nullptr;
    vk::DeviceMemory bufferMemory = nullptr;
    void * mappedMemory = nullptr;
};

// "Backbone" of the renderer, holds functions related to memory/resource allocation
// as well as the Vulkan handles related to their use; largely self-contained
//
// All handles returned by the public functions of this service will need to be manually freed later on by the caller
export class VulkanResourceService {
public:
    VulkanResourceService() {}

    VulkanImageData createTexture(StbImageWrapper & textureImage) {
        VulkanImageData imageData {};

        createTextureImage(textureImage, imageData);
        // TODO choose VkFormat conditionally
        imageData.imageView = createImageView(
            imageData.image,
            vk::Format::eR8G8B8A8Srgb,
            vk::ImageAspectFlagBits::eColor,
            imageData.mipLevels
        );
        imageData.sampler = createTextureSampler();

        return imageData;
    }

    template <typename T>
    VulkanBufferData createVulkanBuffer(vk::DeviceSize bufferSize, vk::BufferUsageFlagBits bufferTypeFlags, std::vector<T> & data) {
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

        void * stagingData = m_Device.mapMemory(stagingBufferMemory, 0, bufferSize);
        memcpy(stagingData, data.data(), bufferSize);
        m_Device.unmapMemory(stagingBufferMemory);

        createBuffer(
            bufferSize,
            vk::BufferUsageFlagBits::eTransferDst | bufferTypeFlags,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            bufferData.buffer,
            bufferData.bufferMemory
        );
        copyBuffer(stagingBuffer, bufferData.buffer, bufferSize);

        m_Device.freeMemory(stagingBufferMemory);
        m_Device.destroyBuffer(stagingBuffer);

        return bufferData;
    }

    std::vector<VulkanUniformBufferData> createUniformBuffers(vk::DeviceSize bufferSize, int maxFramesInFlight) {
        std::vector<VulkanUniformBufferData> uniformBufferData {};

        for (size_t i = 0; i < maxFramesInFlight; ++i) {
            vk::Buffer buffer;
            vk::DeviceMemory bufferMemory;
            createBuffer(
                bufferSize,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                buffer,
                bufferMemory
            );

            uniformBufferData.emplace_back(
                VulkanUniformBufferData {
                    buffer,
                    bufferMemory,
                    m_Device.mapMemory(bufferMemory, 0, bufferSize)
                }
            );
        }

        return uniformBufferData;
    }

    VulkanImageData createGenericResources(
        const vk::Format format,
        const vk::Extent2D extent,
        const vk::SampleCountFlagBits msaaSamples,
        const vk::ImageUsageFlags usageFlags,
        vk::MemoryPropertyFlags memoryProperties,
        const uint32_t mipLevels,
        vk::ImageAspectFlags aspectFlags
    ) {
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

    VulkanImageData createColorResources(const vk::Format colorFormat, const vk::Extent2D swapChainExtent, const vk::SampleCountFlagBits msaaSamples) {
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

    VulkanImageData createDepthResources(const vk::Format depthFormat, const vk::Extent2D swapChainExtent, const vk::SampleCountFlagBits msaaSamples) {
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

        vk::ShaderModule shaderModule = m_Device.createShaderModule(createInfo);
        return shaderModule;
    }

    [[nodiscard]] vk::Device getDevice() const { return m_Device; }
    [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const { return m_PhysicalDevice; }
    [[nodiscard]] vk::Queue getGraphicsQueue() const { return m_GraphicsQueue; }
    [[nodiscard]] vk::CommandPool getCommandPool() const { return m_CommandPool; }

    void setDevice(const vk::raii::Device & device) {
        m_Device = *device;
    }

    void setPhysicalDevice(const vk::raii::PhysicalDevice & physicalDevice) {
        m_PhysicalDevice = *physicalDevice;
    }

    void setGraphicsQueue(const vk::raii::Queue & queue) {
        m_GraphicsQueue = *queue;
    }

    void setCommandPool(const vk::raii::CommandPool & commandPool) {
        m_CommandPool = *commandPool;
    }

private:
    void transitionImageLayout(
        const vk::Image & image,
        const vk::ImageLayout oldLayout,
        const vk::ImageLayout newLayout,
        const uint32_t mipLevels = 1
    ) {
        vk::CommandBuffer commandBuffer = beginSingleTimeCommands();
        transitionImageLayout(commandBuffer, image, oldLayout, newLayout, mipLevels);
        endSingleTimeCommands(commandBuffer);
    }

    void createTextureImage(const StbImageWrapper & textureImage, VulkanImageData & dstImageData) {
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

        void * data = m_Device.mapMemory(stagingBufferMemory, 0, imageSize);
        memcpy(data, textureImage.pixels, imageSize);
        m_Device.unmapMemory(stagingBufferMemory);

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
        m_Device.freeMemory(stagingBufferMemory);
        m_Device.destroyBuffer(stagingBuffer);
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
    ) {
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
        image = m_Device.createImage(imageInfo);

        vk::MemoryRequirements memRequirements = m_Device.getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo = {
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
        };
        deviceMemory = m_Device.allocateMemory(allocInfo);
        m_Device.bindImageMemory(image, deviceMemory, 0);
    }

    vk::CommandBuffer beginSingleTimeCommands() {
        vk::CommandBufferAllocateInfo allocInfo = {
            .commandPool = m_CommandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1
        };
        vk::CommandBuffer commandBuffer = std::move(m_Device.allocateCommandBuffers(allocInfo).front());

        vk::CommandBufferBeginInfo beginInfo { .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
        commandBuffer.begin(beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(vk::CommandBuffer & commandBuffer) {
        commandBuffer.end();

        vk::SubmitInfo submitInfo { .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };
        m_GraphicsQueue.submit(submitInfo, nullptr);
        // using a fence instead of waitIdle() would allow us to schedule multiple transfer simultaneously and
        // wait for all of them to complete instead of executing one at a time. ( = likely better optimization)
        m_GraphicsQueue.waitIdle();

        m_Device.freeCommandBuffers(m_CommandPool, 1, &commandBuffer);
    }

    void copyBuffer(vk::Buffer & srcBuffer, vk::Buffer & dstBuffer, vk::DeviceSize size) {
        auto commandCopyBuffer = beginSingleTimeCommands();
        commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, vk::BufferCopy(0, 0, size));
        endSingleTimeCommands(commandCopyBuffer);
    }

    // TODO parametrize offset/don't create different buffers for every texture
    void createBuffer(
        vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties,
        vk::Buffer & buffer, vk::DeviceMemory & bufferMemory
    ) {
        vk::BufferCreateInfo bufferInfo = {
            .size = size,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };
        buffer = m_Device.createBuffer(bufferInfo);
        vk::MemoryRequirements memRequirements = m_Device.getBufferMemoryRequirements(buffer);
        vk::MemoryAllocateInfo allocInfo = {
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
        };
        bufferMemory = m_Device.allocateMemory(allocInfo);
        m_Device.bindBufferMemory(buffer, bufferMemory, 0);
    }

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
        vk::PhysicalDeviceMemoryProperties memProperties = m_PhysicalDevice.getMemoryProperties();
        // only concerning ourselves about memory types for now, not the heaps that memory comes from
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type!");
    }

    void copyBufferToImage(const vk::Buffer & buffer, vk::Image & image, uint32_t width, uint32_t height) {
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
    void generateMipmaps(vk::Image & image, vk::Format imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
        // Check if image format supports linear blit-ing
        vk::FormatProperties formatProperties = m_PhysicalDevice.getFormatProperties(imageFormat);
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
        return m_Device.createImageView(viewInfo);
    }

    vk::Sampler createTextureSampler() {
        vk::PhysicalDeviceProperties properties = m_PhysicalDevice.getProperties();
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

        return m_Device.createSampler(samplerInfo);
    }

private:
    // Storing hpp-wrapped Vulkan handles instead of RAII objects so that we can work with them through here
    // but still clean them up from elsewhere and not worry about deinitialization order
    // This adds more work when it comes to memory cleanup but that's fine

    vk::Device m_Device = nullptr;
    vk::PhysicalDevice m_PhysicalDevice = nullptr;
    vk::Queue m_GraphicsQueue = nullptr;
    vk::CommandPool m_CommandPool = nullptr;
};
