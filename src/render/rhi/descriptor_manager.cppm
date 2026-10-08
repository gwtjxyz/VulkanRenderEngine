module;

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

#include <cassert>
#include <cstdint>

#include "core/util_macros.h"

export module descriptor_manager;

import texture_manager;
import vulkan_instance;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

#ifndef DISABLE_IMPORT_STD
import std;
#endif

export class DescriptorManager;
GENERATE_LOCATOR(DescriptorManager)

// TODO support descriptor resizing/adjusting on the fly
class DescriptorManager {
public:
    DescriptorManager() = default;

    void setVulkanInstance(VulkanInstance * instance) {
        m_Instance = instance;
    }

    vk::DescriptorSetLayout getDescriptorSetLayout() const {
        return m_TextureSetLayout;
    }

    vk::DescriptorPool getDescriptorPool() const {
        return m_DescriptorPool;
    }

    vk::DescriptorSet getTextureDescriptorSet() const {
        return m_TextureSet;
    }

    void initialize() {
        initializeLayout();
        initializePoolsAndSets();
        initializeSampler();
    }

    void uninitialize() {
        m_TextureSetLayout.clear();
        m_TextureSet.clear();
        m_TextureSampler.clear();
        m_DescriptorPool.clear();
    }

    void writeToDescriptors() const {
        const auto & loadedTextures = TextureManagerLocator::locate()->getTextures();
        std::vector<vk::DescriptorImageInfo> textureDescriptors;

        for (const auto & tex : loadedTextures) {
            vk::DescriptorImageInfo textureInfo = {
                .imageView = tex.imageView,
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            };
            textureDescriptors.push_back(textureInfo);
        }

        vk::DescriptorImageInfo samplerDescriptor = {
            .sampler = m_TextureSampler
        };

        std::array writeDescriptorSets = {
            vk::WriteDescriptorSet {
                .dstSet = m_TextureSet,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &samplerDescriptor
            },
            vk::WriteDescriptorSet {
                .dstSet = m_TextureSet,
                .dstBinding = 1,
                .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = textureDescriptors.data()
            }
        };

        m_Instance->getRaiiDevice().updateDescriptorSets(writeDescriptorSets, {});
    }

private:
    void initializeLayout() {
        std::array bindings = {
            vk::DescriptorSetLayoutBinding(
                0,
                vk::DescriptorType::eSampler,
                1,
                vk::ShaderStageFlagBits::eFragment,
                nullptr
            ),
            vk::DescriptorSetLayoutBinding(
                1,
                vk::DescriptorType::eSampledImage,
                m_InitialTexturePoolSize,
                vk::ShaderStageFlagBits::eFragment,
                nullptr
            )
        };

        std::array bindingFlags = {
            vk::DescriptorBindingFlags {},
            vk::DescriptorBindingFlags {
                vk::DescriptorBindingFlagBits::eVariableDescriptorCount     // size will be specified on set allocation
                | vk::DescriptorBindingFlagBits::ePartiallyBound            // descriptors not used by shader allowed to be invalid
                | vk::DescriptorBindingFlagBits::eUpdateAfterBind           // allow updating after binding to command buffer
            }
        };

        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
            .bindingCount = bindingFlags.size(),
            .pBindingFlags = bindingFlags.data()
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo = {
            .pNext = &bindingFlagsInfo,
            .flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings = bindings.data()
        };

        m_TextureSetLayout = vk::raii::DescriptorSetLayout(m_Instance->getRaiiDevice(), layoutInfo);
    }

    void initializePoolsAndSets() {
        assert(m_Instance && "VulkanInstance must be non-null before attempting to initialize descriptor pools and sets");

        // One texture = one descriptor to allocate
        // Will expand and reallocate descriptor pool if size runs out

        // First item in the pool is the default texture sampler, if necessary can add more
        std::array poolSize = {
            vk::DescriptorPoolSize(
                vk::DescriptorType::eSampler,
                1
            ),
            vk::DescriptorPoolSize(
                vk::DescriptorType::eSampledImage,
                m_InitialTexturePoolSize
            ),
        };
        // free descriptor set bit = valid to return individual allocations to the pool
        // update after bind bit = valid to include update after bind descriptor sets in this pool
        vk::DescriptorPoolCreateInfo poolInfo = {
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet | vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
            .maxSets = 1,
            .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
            .pPoolSizes = poolSize.data()
        };
        m_DescriptorPool = vk::raii::DescriptorPool(m_Instance->getRaiiDevice(), poolInfo);

        vk::DescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAllocInfo = {
            .descriptorSetCount = 1,
            .pDescriptorCounts = &m_InitialTexturePoolSize
        };
        vk::DescriptorSetAllocateInfo textureSetAllocInfo = {
            .pNext = &variableDescCountAllocInfo,
            .descriptorPool = m_DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &*m_TextureSetLayout
        };
        m_TextureSet.clear();
        m_TextureSet = std::move(m_Instance->getRaiiDevice().allocateDescriptorSets(textureSetAllocInfo).front());
    }

    void initializeSampler() {
        vk::PhysicalDeviceProperties properties = m_Instance->getPhysicalDeviceProperties();
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

        m_TextureSampler = m_Instance->getRaiiDevice().createSampler(samplerInfo);
    }
private:
    VulkanInstance * m_Instance = nullptr;

    vk::raii::DescriptorSetLayout m_TextureSetLayout = nullptr;
    vk::raii::DescriptorPool m_DescriptorPool = nullptr;
    vk::raii::DescriptorSet m_TextureSet = nullptr;

    vk::raii::Sampler m_TextureSampler = nullptr;

    const uint32_t m_InitialTexturePoolSize = 100;
    uint32_t m_TexturePoolSize = m_InitialTexturePoolSize;
    uint32_t m_LoadedTextureCount = 0;
};
