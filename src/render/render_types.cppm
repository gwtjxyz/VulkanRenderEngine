module;

#include <cstddef>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE)
#include <vulkan/vulkan.hpp>
#endif

#include <cstdint> // for max value macros

export module render_types;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

import glm;
#if !(defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES) || defined(DISABLE_VULKAN_MODULE))
import vulkan;
#endif

export using hlsl_bool = uint32_t;

export enum class LightMode : int32_t {
    Off = 0,
    Phong = 1,
    Gooch = 2
};

// Replacement for UBOs that we address using "bindless" (buffer device address extension)
export struct MeshData {
    alignas(16) glm::mat4 projection;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 model;
    alignas(16) uint32_t textureIndex = { 0 };
};

export struct LightData {
    alignas(16) glm::vec4 lightPos {};
};

export struct Particle {
    glm::vec4 position;
    glm::vec4 color;
    glm::vec2 velocity;

    static vk::VertexInputBindingDescription getBindingDescription() {
        return {.binding = 0, .stride = sizeof(Particle), .inputRate = vk::VertexInputRate::eVertex};
    }

    // Velocity is only used inside compute shader = don't need to have an attribute description for it
    static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions() {
        return {
            vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Particle, position)),
            vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Particle, color)),
        };
    }
};

// SSBO pointers for compute shaders are referenced from here
export struct ComputePushConstants {
    vk::DeviceAddress addressThisFrame;
    vk::DeviceAddress addressLastFrame;
    float deltaTime;
    uint32_t particlesEnabled;
};

export struct Plane {
    // Some point on the plane
    glm::vec3 point = glm::vec3(0.0f);
    // Plane's normal - unit vector
    glm::vec3 normal = { 0.0f, 1.0f, 0.0f };
};

// TODO
export struct BoundingBox {
    void transform(const glm::mat4 & transformationMatrix) {
        // TODO
    }
};
