module;

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

#include <cassert>
#include <cstdint>

#include "core/util_macros.h"

export module texture_manager;

#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import core_types;
import device_mapper;
import platform;
import vulkan_resource_service;

struct Texture {
    vk::Image image = nullptr;
    vk::DeviceMemory deviceMemory = nullptr;
    vk::ImageView imageView = nullptr;
    bool occupied = false;
};

export class TextureManager;
GENERATE_LOCATOR(TextureManager)

// For now, just allocating an image object per texture should be fine, later can maybe do some fancier allocation stuff
class TextureManager {
public:
    TextureManager() = default;

    void initialize() {
        m_Textures.resize(m_InitialTextureCount);

        // Set default values of each texture as the placeholder texture
        // The system relies on this texture to always be present
        load(pathFromAssetDir("placeholder.png"), m_PlaceholderIndex);

        const auto & [placeholderImage, placeholderMemory, placeholderView, placeholderPresent] = m_Textures.at(m_PlaceholderIndex);
        assert(placeholderPresent && "Placeholder texture must always be present!");
        for (auto & tex : m_Textures) {
            if (!tex.occupied) {
                tex.image = placeholderImage;
                tex.deviceMemory = placeholderMemory;
                tex.imageView = placeholderView;
            }
        }
    }

    uint32_t load(const std::filesystem::path & pathToTexture, const uint32_t materialIndex) {
        const StbImageWrapper imageData(pathToTexture);
        assert(imageData.pixels && "Failed to load data image of texture.");

        auto vulkanImageData = VulkanResourceServiceLocator::locate()->createSampledImageTexture(imageData);

        uint32_t textureIndex = 0;
        for (auto & tex : m_Textures) {
            if (!tex.occupied) {
                tex.image = vulkanImageData.image;
                tex.deviceMemory = vulkanImageData.imageMemory;
                tex.imageView = vulkanImageData.imageView;
                tex.occupied = true;
                break;
            }
            textureIndex++;
        }
        if (textureIndex >= m_Textures.size()) {
            // TODO add code for texture storage expansion
            std::cerr << "[ERROR] No space for texture " << pathToTexture.string() << " !" << std::endl;
            textureIndex = INDEX_UNSET;
            VulkanResourceServiceLocator::locate()->freeResources(vulkanImageData.image, vulkanImageData.imageMemory, vulkanImageData.imageView);
        }

        DeviceMapperLocator::locate()->getMaterial(materialIndex)->layout.textureIndex = textureIndex;
        return textureIndex;
    }

    void unload(const uint32_t materialIndex) {
        if (materialIndex == INDEX_UNSET) {
            return;
        }
        const auto textureIndex = DeviceMapperLocator::locate()->getMaterial(materialIndex)->layout.textureIndex;
        if (textureIndex == INDEX_UNSET) {
            return;
        }

        auto & texture = m_Textures.at(textureIndex);
        if (!texture.occupied) {
            return;
        }

        VulkanResourceServiceLocator::locate()->freeResources(texture.image, texture.deviceMemory, texture.imageView);
        DeviceMapperLocator::locate()->getMaterial(materialIndex)->layout.textureIndex = INDEX_UNSET;

        // Make sure to fill the empty slot with the placeholder texture if we're not shutting down
        // so that Vulkan doesn't freak out, since we're not using null descriptors
        if (materialIndex != m_PlaceholderIndex) {
            const auto & placeholderTexture = m_Textures.at(m_PlaceholderIndex);
            texture.image = placeholderTexture.image;
            texture.deviceMemory = placeholderTexture.deviceMemory;
            texture.imageView = placeholderTexture.imageView;
        }

        texture.occupied = false;


    }

    void unloadAll() {
        const auto * vulkanResourceService = VulkanResourceServiceLocator::locate();
        for (auto & texture : m_Textures) {
            if (texture.occupied) {
                vulkanResourceService->freeResources(texture.image, texture.deviceMemory, texture.imageView);
            }
        }
    }

    const std::vector<Texture> & getTextures() {
        return m_Textures;
    }
private:
    const uint32_t m_PlaceholderIndex = 0;
    const uint32_t m_InitialTextureCount = 100;
    std::vector<Texture> m_Textures {};
};
